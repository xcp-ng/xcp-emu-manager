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
#include <stdio.h>
#include <syslog.h>

#include <xcp-ng/generic.h>

#include "control.h"
#include "emu-client.h"
#include "emu.h"

// =============================================================================

static struct {
  int fdIn;
  int fdOut;

  char bufIn[128];
  size_t bufSize;
  bool waitingAck;
} Xenopsd;

// -----------------------------------------------------------------------------

// Low routine to receive messages.
static inline int control_recv (int timeout) {
  assert(Xenopsd.bufSize <= sizeof Xenopsd.bufIn);
  if (Xenopsd.bufSize == sizeof Xenopsd.bufIn) {
    syslog(LOG_ERR, "Not enough space to read from xenopsd.");
    EmuError = ENOSPC;
    return -1;
  }

  const XcpError ret = xcp_fd_wait_read(
    Xenopsd.fdIn,
    Xenopsd.bufIn + Xenopsd.bufSize,
    sizeof Xenopsd.bufIn - Xenopsd.bufSize,
    timeout
  );
  if (ret == XCP_ERR_TIMEOUT) {
    syslog(LOG_ERR, "Failed to read from xenopsd because timeout reached.");
    EmuError = ETIME;
  } else if (ret == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "Failed to read from xenopsd: `%s`.", strerror(errno));
    EmuError = errno;
  } else if (ret == 0) {
    syslog(LOG_ERR, "Failed to read from xenopsd. Broken pipe.");
    EmuError = EPIPE;
  } else {
    Xenopsd.bufSize += (size_t)ret;
    return (int)ret;
  }

  return -1;
}

// Low routine to send a control message.
static inline int control_send (const char *message) {
  syslog(LOG_DEBUG, "Sending to xenopsd `%s`...", message);

  if (Xenopsd.waitingAck) {
    syslog(LOG_ERR, "Unable to send new message. ACK not received for previous sent message.");
    return -1;
  }

  const size_t len = strlen(message);
  size_t offset;
  if (xcp_fd_write_all(Xenopsd.fdOut, message, len, &offset) == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "Failed to write to xenopsd: `%s`.", strerror(errno));
    EmuError = errno;
    return -1;
  }
  return 0;
}

static inline int control_process_messages () {
  syslog(LOG_DEBUG, "Processing xenopsd messages...");

  static const char restore[] = "restore:";

  int error = 0;

  char *nextMessage;
  int processedMessages = 0;

  char *message = Xenopsd.bufIn;
  do {
    // 1. Read a message and replace new line char to NULL.
    nextMessage = memchr(message, '\n', Xenopsd.bufSize);
    if (!nextMessage) {
      memmove(Xenopsd.bufIn, message, Xenopsd.bufSize);
      if (Xenopsd.bufSize == sizeof Xenopsd.bufIn) {
        syslog(LOG_ERR, "Unable to process xenopsd message. Buffer is so big!");
        EmuError = EMSGSIZE;
        return -1;
      }
      return 0; // No complete message to read for the moment.
    }
    *nextMessage++ = '\0';
    syslog(LOG_DEBUG, "Processing xenopsd message: `%s`.", message);

    // 2. Process message.
    if (!strcmp(message, "done")) {
      if (!Xenopsd.waitingAck) {
        syslog(LOG_ERR, "Unexpected ACK received from xenopsd.");
        error = EINVAL;
      } else {
        Xenopsd.waitingAck = false;
        ++processedMessages;
      }
    } else if (!strncmp(message, restore, sizeof restore - 1)) {
      Emu *emu = emu_from_name(message + sizeof restore - 1);
      if (!emu) {
        syslog(LOG_ERR, "Unable to restore from xenopsd for unknown emu: `%s`.", message + sizeof restore - 1);
        error = EINVAL;
      } else if (emu->state != EMU_STATE_INITIALIZED) {
        syslog(LOG_ERR, "Request to restore emu `%s` already in progress.", emu->name);
        error = EINVAL;
      } else {
        emu->state = EMU_STATE_RESTORING;
        if (emu_set_stream_busy(emu, true) > -1 && emu_client_send_emp_cmd(emu->client, cmd_restore, NULL) > -1)
          ++processedMessages;
      }
    } else if (!strcmp(message, "abort")) {
      syslog(LOG_DEBUG, "Received abort command from xenopsd.");
      error = ESHUTDOWN;
    } else {
      syslog(LOG_ERR, "Unexpected message from xenopsd: `%s`.", message);
      error = EINVAL;
    }

    // 3. Set new message pointer.
    Xenopsd.bufSize -= (size_t)(nextMessage - message);
    message = nextMessage;
  } while (!error);

  memmove(Xenopsd.bufIn, nextMessage, Xenopsd.bufSize);
  if (error) {
    EmuError = error;
    return -1;
  }
  return processedMessages;
}

// -----------------------------------------------------------------------------

int control_init (int fdIn, int fdOut) {
  Xenopsd.fdIn = fdIn;
  Xenopsd.fdOut = fdOut;
  Xenopsd.bufSize = 0;
  Xenopsd.waitingAck = false;

  return 0;
}

// -----------------------------------------------------------------------------

int control_get_fd_in () {
  return Xenopsd.fdIn;
}

// -----------------------------------------------------------------------------

int control_receive_and_process_messages (int timeout) {
  syslog(LOG_DEBUG, "Receiving and processing xenopsd messages...");

  int ret;
  do {
    if (control_recv(timeout) < 0)
      return -1;

    if ((ret = control_process_messages()) < 0)
      return -1;
  } while (Xenopsd.waitingAck);

  return ret;
}

int control_send_prepare (const char *emuName) {
  // Do not check snprintf truncation because emu name length is normally small.
  char buf[128];
  if (snprintf(buf, sizeof buf, "prepare:%s\n", emuName) < 0) {
    syslog(LOG_ERR, "Failed to fill control_send_prepare buffer: `%s`.", strerror(errno));
    EmuError = errno;
  } else if (control_send(buf) > -1) {
    Xenopsd.waitingAck = true;
    return control_receive_and_process_messages(120000);
  }

  return -1;
}

int control_send_suspend () {
  if (control_send("suspend:\n") < 0)
    return -1;
  Xenopsd.waitingAck = true;
  return control_receive_and_process_messages(120000);
}

int control_send_progress (int progress) {
  static int previousProgress = -1;
  if (previousProgress != progress) {
    int error = 0;
    char buf[128];
    if (snprintf(buf, sizeof buf, "info:\\b\\b\\b\\b%d\n", progress) < 0) {
      syslog(LOG_ERR, "Failed to format progress buffer: `%s`.", strerror(errno));
      error = errno;
    } else
      error = control_send(buf);

    previousProgress = progress;
    if (!error)
      return progress;
  }

  return previousProgress;
}

int control_send_result (const char *emuName, const char *result) {
  char buf[128];
  int ret;
  if (!result)
    ret = snprintf(buf, sizeof buf, "result:%s\n", emuName);
  else
    ret = snprintf(buf, sizeof buf, "result:%s %s\n", emuName, result);

  if (ret < 0) {
    syslog(LOG_ERR, "Failed to format result.");
    EmuError = errno;
    return -1;
  }

  if ((size_t)ret >= sizeof buf) {
    syslog(LOG_ERR, "Failed to format result. Truncated buffer!");
    EmuError = EMSGSIZE;
    return -1;
  }

  if (control_send(buf) < 0)
    return -1;

  return 0;
}

int control_send_final_result () {
  if (control_send("result:0 0\n") < 0)
    return -1;
  return 0;
}

int control_report_error (int errorCode) {
  const char *emuName = NULL;
  Emu *emu = emu_manager_find_first_failed();
  if (emu) {
    errorCode = emu->errorCode;
    emuName = emu->name;
  }

  char buf[128];
  if (snprintf(buf, sizeof buf, "error:%s%s%s\n", emuName ? emuName : "", emuName ? " " : "", emu_error_code_to_str(errorCode)) < 0) {
    syslog(LOG_ERR, "Unable to report error: `%s`.", strerror(errno));
    EmuError = errno;
    return -1;
  }
  syslog(LOG_INFO, "Reporting: `%s`...", buf);
  return control_send(buf);
}
