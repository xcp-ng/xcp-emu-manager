/*
 * xcp-emu-manager
 * Copyright (C) 2019  Vates SAS - ronan.abhamon@vates.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include <xcp-ng/generic.h>

#include "arg-list.h"
#include "emu-client.h"
#include "emu.h"

// =============================================================================

static int format_message (char *buf, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = vsnprintf(buf, size, format, args);
  va_end(args);

  if (ret < 0) {
    syslog(LOG_ERR, "Failed to format command.");
    EmuError = errno;
    return -1;
  }

  if ((size_t)ret >= size) {
    syslog(LOG_ERR, "Failed to format command. Truncated buffer!");
    EmuError = EMSGSIZE;
    return -1;
  }

  return ret;
}

static inline int emu_client_send_cmd (EmuClient *client, const char *command, int fd, const ArgNode *arguments) {
  syslog(LOG_DEBUG, "Sending command `%s` to emu client `%s`.", command, client->emu->name);

  // 1. Create a command string.
  const char *format = arguments
    ? "{ \"execute\" : \"%s\", \"arguments\" : { "
    : "{ \"execute\" : \"%s\" }";

  char buf[1024];
  int ret;
  if ((ret = format_message(buf, sizeof buf, format, command)) < 0)
    return -1;

  // 2. Format arguments.
  if (arguments) {
    size_t remaining = sizeof buf - (size_t)ret;
    char *pos = buf + ret;

    do {
      const char *separator = arguments->next ? "," : "";
      if ((ret = format_message(pos, remaining, "\"%s\":%s%s ", arguments->key, arguments->value, separator)) < 0)
        return -1;

      arguments = arguments->next;
      remaining -= (size_t)ret;
      pos += ret;
    } while (arguments);

    if (format_message(pos, remaining, "} }") < 0)
      return -1;
  }

  // 3. Send the command.
  if (fd < 0) {
    syslog(LOG_DEBUG, "Sending message \'%s\' to emu client.", buf);
    size_t offset;
    ret = (int)xcp_fd_write_all(client->fd, buf, strlen(buf), &offset);
  } else {
    syslog(LOG_DEBUG, "Sending message \'%s\' to emu client with shared socket: %d.", buf, fd);
    ret = (int)xcp_sock_send_shared_fd(client->fd, buf, strlen(buf), fd);
  }

  if (ret == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "Error sending message to emu client: `%s`.", strerror(errno));
    EmuError = errno;
    return -1;
  }

  // 4. Waiting for ACK...
  client->waitingAck = true;
  while (client->waitingAck)
    if (emu_client_receive_events(client, 30000) < 0 || emu_client_process_events(client) < 0)
      return -1;

  return 0;
}

// -----------------------------------------------------------------------------

int emu_client_create (EmuClient **client, EmuClientCb eventCb, Emu *emu) {
  if (!(*client = malloc(sizeof **client)))
    goto fail;

  if (!((*client)->tokener = json_tokener_new())) {
    free(*client);
    goto fail;
  }

  (*client)->fd = -1;
  (*client)->emu = emu;
  (*client)->eventCb = eventCb;
  (*client)->bufSize = 0;
  (*client)->waitingAck = false;

  return 0;

fail:
  syslog(LOG_ERR, "Not enough memory to create EmuClient.");
  free(*client);
  EmuError = errno;
  return -1;
}

int emu_client_destroy (EmuClient *client) {
  int error = 0;
  if (client->fd > -1 && xcp_fd_close(client->fd) == XCP_ERR_ERRNO)
    error = errno;

  json_tokener_free(client->tokener);
  free(client);

  if (error) {
    EmuError = error;
    return -1;
  }
  return 0;
}

// -----------------------------------------------------------------------------

int emu_client_connect (EmuClient *client, const char *path) {
  if (strlen(path) >= XCP_SOCK_UNIX_PATH_MAX) {
    EmuError = ENAMETOOLONG;
    return -1;
  }

  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    syslog(LOG_ERR, "Unable to create socket: `%s`.", strerror(errno));
    EmuError = errno;
    return -1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof addr);
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);

  if (xcp_sock_connect(fd, (struct sockaddr *)&addr, sizeof addr) == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "Unable to connect socket: `%s`.", strerror(errno));
    EmuError = errno;
    xcp_fd_close(fd);
    return -1;
  }

  client->fd = fd;
  return 0;
}

// -----------------------------------------------------------------------------

int emu_client_receive_events (EmuClient *client, int timeout) {
  assert(client->bufSize <= sizeof client->buf);
  if (client->bufSize == sizeof client->buf) {
    syslog(LOG_ERR, "Not enough space to read from EmuClient.");
    EmuError = ENOSPC;
    return -1;
  }

  const XcpError ret = xcp_fd_wait_read(
    client->fd,
    client->buf + client->bufSize,
    sizeof client->buf - client->bufSize,
    timeout
  );
  if (ret == XCP_ERR_TIMEOUT) {
    syslog(LOG_ERR, "EmuClient `%s` failed to read because timeout reached.", client->emu->name);
    EmuError = ETIME;
  } else if (ret == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "EmuClient `%s` failed to read from %d: `%s`.", client->emu->name, client->fd, strerror(errno));
    EmuError = errno;
  } else if (ret == 0) {
    syslog(LOG_ERR, "EmuClient `%s` unexpectedly disconnected. Broken pipe.", client->emu->name);
    EmuError = EPIPE;
  } else {
    client->bufSize += (size_t)ret;
    return (int)ret;
  }

  return -1;
}

// -----------------------------------------------------------------------------

int emu_client_process_events (EmuClient *client) {
  if (!client->bufSize) return 0;

  syslog(LOG_DEBUG, "Processing emu client events from `%s`...", client->emu->name);

  int error = 0;
  char *jsonBuf = client->buf;
  do {
    json_tokener_reset(client->tokener);
    json_object *obj = json_tokener_parse_ex(client->tokener, jsonBuf, (int)client->bufSize);
    const enum json_tokener_error tokenError = json_tokener_get_error(client->tokener);
    if (tokenError == json_tokener_continue) {
      if (client->bufSize == sizeof client->buf) {
        syslog(LOG_ERR, "Unable to process emu client events. Buffer is so big!");
        error = EMSGSIZE;
      }
      break; // Error or no complete event to read for the moment.
    }
    if (tokenError != json_tokener_success) {
      memmove(client->buf, jsonBuf, client->bufSize);
      syslog(LOG_ERR, "Error from tokener: `%s`.", json_tokener_error_desc(tokenError));
      error = EINVAL;
      break;
    }
    assert(obj);

    syslog(LOG_DEBUG, "Processing emu client event: `%.*s`.", client->tokener->char_offset, jsonBuf);

    const json_type type = json_object_get_type(obj);
    if (type != json_type_object) {
      syslog(LOG_ERR, "Expected JSON object from emu client but got type: %d.", type);
      break;
    }

    const struct lh_entry *entry = json_object_get_object(obj)->head;
    if (entry) {
      const json_object *eventType = NULL;
      const json_object *data = NULL;
      const json_object *qmpValue = NULL;

      // TODO: Use for.
      do {
        const char *key = entry->k;
        json_object *value = (json_object *)entry->v;
        entry = entry->next;

        if (!strcmp(key, "return")) {
          if (!client->waitingAck) {
            syslog(LOG_ERR, "Unexpected `return` event from emu client.");
            error = EINVAL;
          } else
          client->waitingAck = false;
        } else if (!strcmp(key, "error")) {
          if (!json_object_is_type(value, json_type_string))
            syslog(LOG_ERR, "Unknown error from emu client: `%s`", json_object_to_json_string(value));
          else
            syslog(LOG_ERR, "Error from emu client: `%s`.", json_object_get_string(value));
          error = EINVAL;
        } else if (!strcmp(key, "event")) {
          if (json_object_get_type(value) == json_type_string)
            eventType = value;
        } else if (!strcmp(key, "data")) {
          data = value;
        } else if (!strcmp(key, "QMP")) {
          if (json_object_get_type(value) == json_type_object)
            qmpValue = value;
        } else if (!strcmp(key, "timestamp")) {
          syslog(LOG_DEBUG, "Ignoring QMP timestamp.");
        } else if (json_object_get_type(value) != json_type_object) {
          syslog(LOG_ERR, "Unexpected key from emu client: `%s`.", key);
          error = EINVAL;
        }
      } while (entry && !error);

      if (!error) {
        if (!eventType) {
          if (data) {
            syslog(LOG_ERR, "Emu client sent data without event!");
            error = EINVAL;
          } else if (qmpValue && client->eventCb)
            (*client->eventCb)(client, "QMP", qmpValue);
        } else if (client->eventCb)
          (*client->eventCb)(client, json_object_get_string((json_object *)eventType), data);
      }
    }

    json_object_put(obj);
    jsonBuf += client->tokener->char_offset;
    client->bufSize -= (size_t)client->tokener->char_offset;
  } while (client->bufSize && !error);

  memmove(client->buf, jsonBuf, client->bufSize);
  if (error) {
    EmuError = error;
    return -1;
  }
  return 0;
}

// -----------------------------------------------------------------------------

int emu_client_send_emp_cmd (EmuClient *client, EmpCommandNum cmdNum, const ArgNode *arguments) {
  const struct command *cmd = command_from_num(cmdNum);
  assert(!cmd->needs_fd);
  return emu_client_send_cmd(client, cmd->name, -1, arguments);
}

int emu_client_send_emp_cmd_with_fd (EmuClient *client, EmpCommandNum cmdNum, int fd, const ArgNode *arguments) {
  const struct command *cmd = command_from_num(cmdNum);
  assert(!cmd->needs_fd || fd >= 0);
  return emu_client_send_cmd(client, cmd->name, cmd->needs_fd ? fd : -1, arguments);
}

int emu_client_send_qmp_cmd (EmuClient *client, QmpCommandNum cmdNum, const ArgNode *arguments) {
  return emu_client_send_cmd(client, qmp_command_from_num(cmdNum), -1, arguments);
}
