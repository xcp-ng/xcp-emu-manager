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

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#include <xcp-ng/generic.h>

#include "arg-list.h"
#include "control.h"
#include "emu-client.h"
#include "emu.h"

// =============================================================================

#define EMU_LOG_PHASE() syslog(LOG_DEBUG, "Phase: %s", __func__)

static int emu_manager_process (bool (*cb)(Emu *emu));

// =============================================================================

typedef struct EmuStream {
  int fd;
  bool isBusy;
  int remainingUses;
  int refCount;
} EmuStream;

// =============================================================================

__thread int EmuError;

// All supported and used emus.
// By default only xenguest is enabled.
// qemu is enabled here: https://github.com/xapi-project/xenopsd/blob/ddc965e3d5bcdb2a77c387237a9ea77eddfc3b43/xc/domain.ml#L874
static Emu Emus[] = {
  {
    .name = "xenguest",
    .pathName = "/usr/libexec/xen/bin/xenguest",
    .type = EmuTypeEmp,
    .flags =
      EMU_FLAG_ENABLED |
      EMU_FLAG_MIGRATE_LIVE |
      EMU_FLAG_WAIT_LIVE_STAGE_DONE |
      EMU_FLAG_MIGRATE_PAUSED,
    .state = EMU_STATE_INITIALIZED,
    .progress = { .fakeTotal = 1024 * 1024 }
  }, {
    .name = "qemu",
    .pathName = NULL,
    .type = EmuTypeQmpLibxl,
    .flags =
      EMU_FLAG_MIGRATE_LIVE |
      EMU_FLAG_MIGRATE_PAUSED,
    .state = EMU_STATE_UNINITIALIZED,
    .progress = { .fakeTotal = 640 * 1024 }
  }
};

static volatile sig_atomic_t WaitEmusTermination;

// =============================================================================
// Emu.
// =============================================================================

static int emu_json_check_type (const char *key, json_object *value, json_type expectedType) {
  const json_type currentType = json_object_get_type(value);
  if (currentType != expectedType) {
    syslog(LOG_ERR, "Unexpected event type for key `%s`. (Current=%d, Expected=%d)", key, currentType, expectedType);
    EmuError = EINVAL;
    return -1;
  }
  return 0;
}

#define SENT_SMOOTH_RATIO (80.f / 100.f)

static int emu_manager_compute_progress () {
  int64_t total = 0;
  int64_t amount = 0;

  Emu *emu;
  foreach (emu, Emus) {
    if (!emu->flags) continue;

    const EmuMigrationProgress *progress = &emu->progress;
    if (progress->iteration < 0) {
      total += progress->fakeTotal;
      if (emu->state > EMU_STATE_LIVE_STAGE_DONE)
        amount += progress->fakeTotal;
    } else {
      total += progress->sent + progress->remaining;
      amount += progress->sent +
        (int64_t)((float)(progress->sentMidIteration - progress->sent) * SENT_SMOOTH_RATIO);
    }
  }

  return total ? (int)(amount * 100 / total) : 0;
}

static int emu_manager_send_progress () {
  const int progress = emu_manager_compute_progress();
  return control_send_progress(progress) > -1 ? progress : -1;
}

// -----------------------------------------------------------------------------
// Emu process callbacks.
// -----------------------------------------------------------------------------

static bool emu_process_cb_wait_qmp_libxl_initialization (Emu *emu) {
  if (emu->type != EmuTypeQmpLibxl)
    return false; // Initialized because it's not a QMP libxl emu.

  if (emu->qmpConnectionEstablished) {
    // QMP connection is established but we must execute "qmp_capabilities"
    // to enter command mode.
    if (emu->state == EMU_STATE_UNINITIALIZED) {
      // It's not necessary to check the return, after the next send command
      // an error must be returned.
      emu_client_send_qmp_cmd(emu->client, QmpCommandNumCapabilities, NULL);
      emu->state = EMU_STATE_INITIALIZED;
    }

    // Initialized. \o/
    return false;
  }

  // Not initialized.
  return true;
}

static bool emu_process_cb_wait_live_stage_done (Emu *emu) {
  return (emu->flags & EMU_FLAG_WAIT_LIVE_STAGE_DONE) && emu->state != EMU_STATE_LIVE_STAGE_DONE;
}

static bool emu_process_cb_wait_migrate_live_finished (Emu *emu) {
  return (emu->flags & EMU_FLAG_MIGRATE_LIVE) && emu->state != EMU_STATE_MIGRATION_DONE;
}

// -----------------------------------------------------------------------------
// Emu client event callbacks.
// -----------------------------------------------------------------------------

static int emu_client_event_cb_emp (EmuClient *client, const char *eventType, const json_object *obj) {
  if (strcmp(eventType, "MIGRATION")) {
    syslog(LOG_ERR, "Unknown event type: `%s`.", eventType);
    EmuError = EINVAL;
    return -1;
  }

  const struct lh_entry *entry = json_object_get_object((json_object *)obj)->head;
  if (!entry) return 0; // Ignore empty object.

  int iterationValue = -1;
  int64_t remainingValue = -1;
  int64_t sentValue = -1;

  EmuMigrationProgress *progress = &client->emu->progress;

  for (; entry; entry = entry->next) {
    const char *key = entry->k;
    json_object *value = (json_object *)entry->v;

    if (!strcmp(key, "status")) {
      if (emu_json_check_type(key, value, json_type_string) < 0)
        return -1;

      const char *status = json_object_get_string(value);
      if (strcmp(status, "completed")) {
        syslog(LOG_ERR, "Invalid emu `%s` event status: `%s`.", client->emu->name, status);
        EmuError = EREMOTEIO;
        return -1;
      }

      syslog(LOG_INFO, "Emu `%s` is completed.", client->emu->name);
      client->emu->state = EMU_STATE_MIGRATION_DONE;
      if (emu_set_stream_busy(client->emu, false) < 0)
        return -1;
    } else if (!strcmp(key, "result")) {
      if (emu_json_check_type(key, value, json_type_string) < 0)
        return -1;

      const char *result = json_object_get_string(value);
      syslog(LOG_DEBUG, "Emu %s received result: `%s`.", client->emu->name, result);

      free(progress->result);
      if (!(progress->result = strdup(result))) {
        syslog(LOG_ERR, "Failed to copy event result buffer.");
        EmuError = errno;
        return -1;
      }
    } else {
      if (emu_json_check_type(key, value, json_type_int) < 0)
        return -1;

      if (!strcmp(key, "remaining"))
        remainingValue = json_object_get_int64(value);
      else if (!strcmp(key, "sent"))
        sentValue = json_object_get_int64(value);
      else if (!strcmp(key, "iteration"))
        iterationValue = json_object_get_int(value);
      else {
        syslog(LOG_ERR, "Unexpected event data key: `%s`", key);
        EmuError = EINVAL;
        return -1;
      }
    }
  }

  if (iterationValue < 0 && remainingValue < 0)
    return 0;

  if (iterationValue == 0 && remainingValue == 0)
    remainingValue = -1;
  else if (remainingValue != -1) {
    progress->sent = sentValue;
    progress->remaining = remainingValue;
    progress->iteration = iterationValue;
  }
  progress->sentMidIteration = sentValue;

  const int sentProgress = emu_manager_send_progress();
  if (sentProgress < 0) return -1;

  syslog(LOG_INFO, "Event for `%s`: rem %ld, sent %ld, iter %d, %s. Progress = %d",
    client->emu->name,
    remainingValue,
    sentValue,
    iterationValue,
    client->emu->state == EMU_STATE_LIVE_STAGE_DONE ? "waiting" : "not waiting",
    sentProgress
  );

  // TODO: Check better remaining value.
  if (
    iterationValue > 0 &&
    (remainingValue <= 50 || iterationValue >= 4) &&
    client->emu->state != EMU_STATE_LIVE_STAGE_DONE
  ) {
    syslog(LOG_INFO, "`%s` live stage is done!", client->emu->name);
    client->emu->state = EMU_STATE_LIVE_STAGE_DONE;
  }

  return 0;
}

static int emu_client_event_cb_qmp_libxl (EmuClient *client, const char *eventType, const json_object *obj) {
  XCP_UNUSED(obj);

  if (!strcmp(eventType, "QMP")) {
    syslog(LOG_INFO, "Got QMP version negotiation.");
    client->emu->qmpConnectionEstablished = true;
  } else
    syslog(LOG_INFO, "Ignoring QMP event: `%s`.", eventType);

  return 0;
}

// -----------------------------------------------------------------------------

static void emu_handle_error (Emu *emu, int errorCode, const char *label) {
  // Because many emus can fail, it's necessary to mark the first failed emu,
  // and then report it to xenopsd in the termination process.
  static bool firstEmuError = true;

  syslog(LOG_ERR, "Error for emu `%s`: %s => %s", emu->name, label, emu_error_code_to_str(errorCode));
  if (errorCode && emu->errorCode == 0) {
    emu->errorCode = errorCode;
    emu->isFirstFailedEmu = firstEmuError;
    firstEmuError = false;
  }

  return;
}

// -----------------------------------------------------------------------------

static int emu_fork_emp_client (Emu *emu, uint domId) {
  assert(emu->pathName);

  char *argv[] = {
    (char *)emu->pathName,
    "-debug",
    "-domid",
    NULL,
    "-controloutfd",
    "2",
    "-controlinfd",
    "0",
    "-mode",
    "listen",
    NULL
  };

  syslog(LOG_INFO, "Starting `%s`...\n", *argv);

  // Exec!
  int pipefd[2];
  if (pipe(pipefd) < 0) {
    syslog(LOG_ERR, "Unable to create pipe: `%s`.", strerror(errno));
    EmuError = errno;
    return -1;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    xcp_fd_close(pipefd[0]);
    xcp_fd_close(pipefd[1]);
    syslog(LOG_ERR, "Error starting `%s`: `%s`.", *argv, strerror(errno));
    EmuError = errno;
    return -1;
  }

  // Child process.
  if (pid == 0) {
    if (xcp_fd_dup(pipefd[1], STDOUT_FILENO) < 0) {
      syslog(LOG_ERR, "Failed to dup.\n");
      exit(EXIT_FAILURE);
    }

    xcp_fd_close(pipefd[0]);
    xcp_fd_close(pipefd[1]);
    clearenv();

    if (
      putenv("LD_PRELOAD=/usr/libexec/coreutils/libstdbuf.so") ||
      putenv("_STDBUF_O=0")
    ) {
      syslog(LOG_ERR, "Failed to putenv.\n");
      exit(EXIT_FAILURE);
    }

    if (asprintf(&argv[3], "%u", domId) < 0) {
      syslog(LOG_ERR, "Unable to format domId: `%s`.", strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (execvp(*argv, argv) < 0) {
      syslog(LOG_ERR, "Failed to start `%s` process.", *argv);
      exit(EXIT_FAILURE);
    }
  }

  // Main process.
  emu->pid = pid;
  xcp_fd_close(pipefd[1]);

  static const char ready[] = "Ready\n";

  char buf[sizeof ready - 1];
  size_t offset;
  const XcpError ret = xcp_fd_read_all(pipefd[0], buf, sizeof buf, 180 * 1000, &offset);
  xcp_fd_close(pipefd[0]);

  if (ret == XCP_ERR_TIMEOUT) {
    syslog(LOG_ERR, "Failed to read from `%s` because timeout reached.", *argv);
    EmuError = ETIME;
  } else if (ret == XCP_ERR_ERRNO) {
    syslog(LOG_ERR, "Failed to read from `%s`: `%s`.", *argv, strerror(errno));
    EmuError = errno;
  } else if (ret == 0) {
    syslog(LOG_ERR, "Failed to read from `%s`. Pipe is broken.", *argv);
    EmuError = EPIPE;
  } else if (memcmp(buf, ready, sizeof ready - 1)) {
    syslog(LOG_ERR, "Invalid output given by `%s`.", *argv);
    EmuError = EINVAL;
  } else
    return 0;

  return -1;
}

// -----------------------------------------------------------------------------

static int emu_connect (Emu *emu, uint domId) {
  if (!emu->flags) return 0;

  // Do not check snprintf truncation. Buffer size is normally sufficient.
  char buf[64];
  EmuClientCb eventCb = NULL;
  if (emu->type == EmuTypeEmp) {
    if (snprintf(buf, sizeof buf, "/run/xen/%s-control-%d", emu->name, domId) < 0) {
      EmuError = errno;
      goto fail;
    }
    eventCb = emu_client_event_cb_emp;
  } else if (emu->type == EmuTypeQmpLibxl) {
    if (snprintf(buf, sizeof buf, "/var/run/xen/qmp-libxl-%d", domId) < 0) {
      EmuError = errno;
      goto fail;
    }
    eventCb = emu_client_event_cb_qmp_libxl;
  }
  assert(eventCb);
  syslog(LOG_INFO, "Connecting to `%s` (%s)...", emu->name, buf);

  if (emu_client_create(&emu->client, eventCb, emu) < 0)
    goto fail;

  if (emu_client_connect(emu->client, buf) < 0)
    goto fail;

  return 0;

fail:
  syslog(LOG_ERR, "Failed to connect to `%s`!", emu->name);
  return -1;
}

static int emu_disconnect (Emu *emu) {
  int error = 0;

  // 1. Destroying client...
  EmuClient *client = emu->client;
  if (client) {
    if (emu->pathName && client->fd > -1 && emu_client_send_emp_cmd(client, cmd_quit, NULL) < 0)
      error = EmuError;

    if (emu_client_destroy(client) < 0 && !error)
      error = EmuError;
    emu->client = NULL;
  }

  // 2. Destroying stream...
  EmuStream *stream = emu->stream;
  if (stream) {
    emu->stream = NULL;

    assert(stream->refCount > 0);
    if (--stream->refCount == 0) {
      if (stream->fd > -1) {
        syslog(LOG_DEBUG, "Closing fd %d, before freeing for `%s`...", stream->fd, emu->name);
        if (xcp_fd_close(stream->fd) == XCP_ERR_ERRNO)
          syslog(LOG_ERR, "Failed to close stream fd for emu `%s`: `%s`.", emu->name, strerror(errno));
      }
      free(stream);
    }
  }

  emu->flags = 0;

  if (error) {
    EmuError = error;
    return -1;
  }
  return 0;
}

// -----------------------------------------------------------------------------

static int emu_init (Emu *emu) {
  if (!emu->flags) return 0;

  EmuStream *stream = emu->stream;
  if (stream && stream->remainingUses == 0) {
    syslog(LOG_ERR, "Unable to use stream fd when remaining uses is 0.");
    EmuError = EINVAL;
    return -1;
  }

  if (emu->type == EmuTypeQmpLibxl) {
    syslog(LOG_DEBUG, "Waiting for QEMU...");
    if (emu_manager_process(emu_process_cb_wait_qmp_libxl_initialization) < 0)
      return -1;
    syslog(LOG_DEBUG, "QEMU is ready!");
    return 0;
  }

  if (stream) {
    if (emu_client_send_emp_cmd_with_fd(emu->client, cmd_migrate_init, stream->fd, NULL) < 0)
      return -1;

    // Check if we can use stream with a valid state.
    if (!stream->remainingUses) {
      if (stream->fd > -1) {
        syslog(LOG_ERR, "Unable to use the `%s` opened stream fd when remaining uses is 0.", emu->name);
        EmuError = EINVAL;
        return -1;
      }
    } else if (stream->fd <= -1) {
      syslog(LOG_ERR, "Unable to use the `%s` closed stream fd when remaining uses is %d.", emu->name, stream->remainingUses);
      EmuError = EINVAL;
      return -1;
    }

    // Stream is open and state is valid at this point.
    if (--stream->remainingUses == 0) {
      syslog(LOG_DEBUG, "Closing emu stream `%s`...", emu->name);
      const int fd = stream->fd;
      stream->fd = -1;

      if (xcp_fd_close(fd) == XCP_ERR_ERRNO) {
        syslog(LOG_ERR, "Failed to close stream fd for emu `%s` because: `%s`.", emu->name, strerror(errno));
        EmuError = errno;
        return -1;
      }
    }
  }

  if (emu->arguments && emu_client_send_emp_cmd(emu->client, cmd_set_args, emu->arguments) < 0)
    return -1;

  return 0;
}

// -----------------------------------------------------------------------------

#define EMU_ERROR_OFFSET -2

const char *emu_error_code_to_str (int errorCode) {
  static const char *errors[] = {
    "unexpectedly disconnected",
    "was killed by a signal",
    "exited with an error"
  };

  if (errorCode >= 1)
    return strerror(errorCode);
  if (errorCode > EMU_ERROR_OFFSET - (int)XCP_ARRAY_LEN(errors) && errorCode <= EMU_ERROR_OFFSET)
    return errors[-errorCode + EMU_ERROR_OFFSET];
  return "erroneous";
}

Emu *emu_from_name (const char *name) {
  Emu *emu;
  foreach (emu, Emus)
    if (!strcmp(name, emu->name))
      return emu;
  return NULL;
}

// =============================================================================
// EmuStream.
// =============================================================================

int emu_create_stream (Emu *emu, int fd) {
  assert(fd > -1);

  if (emu->stream) {
    syslog(LOG_ERR, "Emu `%s` cannot have more then one stream: (first=%d, second=%d).", emu->name, emu->stream->fd, fd);
    EmuError = EINVAL;
    return -1;
  }

  // Check if descriptor already exists on other emu.
  Emu *otherEmu;
  foreach (otherEmu, Emus)
    if (otherEmu->stream && otherEmu->stream->fd == fd) {
      ++otherEmu->stream->remainingUses;
      ++otherEmu->stream->refCount;
      emu->stream = otherEmu->stream;
      return 0;
    }

  // Otherwise create new stream.
  EmuStream *newStream = malloc(sizeof *newStream);
  if (!newStream) {
    syslog(LOG_ERR, "Failed to allocate stream.");
    EmuError = errno;
    return -1;
  }

  newStream->fd = fd;
  newStream->isBusy = false;
  newStream->remainingUses = 1;
  newStream->refCount = 1;

  // Check fd type and mode.
  struct stat buf;
  if (fstat(fd, &buf) < 0) {
    EmuError = errno;
    goto fail;
  }

  // If fd is a socket or a pipe (migration case: save/restore), no problem,
  // we have finished creating the stream, otherwise we must check the write flags
  // of the fd file.
  if (!S_ISSOCK(buf.st_mode) && !S_ISFIFO(buf.st_mode)) {
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
      EmuError = errno;
      goto fail;
    }
    // Ensure we can write and append data in the file (suspend case).
    if ((flags & O_ACCMODE) != O_RDONLY && !(flags & O_APPEND)) {
      syslog(LOG_ERR, "File descriptor %d is a file with flags: %x.", fd, flags);
      EmuError = ENOSTR;
      goto fail;
    }
  }

  emu->stream = newStream;
  return 0;

fail:
  syslog(LOG_ERR, "Failed to validate stream %d for `%s`: `%s`.", fd, emu->name, strerror(EmuError));
  free(newStream);
  return -1;
}

int emu_set_stream_busy (Emu *emu, bool status) {
  EmuStream *stream = emu->stream;
  assert(stream);

  if (stream->isBusy == status) {
    syslog(LOG_ERR, "Unable to set stream as %s when already in this state.", stream->isBusy ? "busy" : "idle");
    EmuError = EINVAL;
    return -1;
  }

  stream->isBusy = status;

  return 0;
}

// =============================================================================
// EmuManager.
// =============================================================================

static void emu_manager_termination_timeout_handler () { WaitEmusTermination = false; }

static int emu_manager_poll () {
  // 1. Constructs fds array to poll.
  uint fdCount = 0;
  struct pollfd fds[8] = { { 0 } };
  Emu *emusToCheck[8];

  fds[fdCount].fd = control_get_fd_in();
  fds[fdCount++].events = POLLIN;

  Emu *emu;
  foreach (emu, Emus)
    if (emu->flags) {
      const int fd = emu->client->fd;
      if (fd <= -1) {
        syslog(LOG_ERR, "Unable to poll with invalid fd in emu `%s`.", emu->name);
        EmuError = EINVAL;
        return -1;
      }

      fds[fdCount].fd = fd;
      fds[fdCount].events = POLLIN;
      emusToCheck[fdCount++] = emu;
    }

  if (fdCount > XCP_ARRAY_LEN(fds)) {
    syslog(LOG_ERR, "Too many fds to poll!");
    EmuError = EINVAL;
    return -1;
  }

  syslog(LOG_DEBUG, "Polling %d socks...", fdCount);

  // 2. Poll!
  const XcpError ret = xcp_poll(fds, fdCount, 30000);
  if (ret == XCP_ERR_TIMEOUT) {
    EmuError = ETIME;
    return -1;
  }
  if (ret == XCP_ERR_ERRNO) {
    EmuError = errno;
    return -1;
  }

  for (uint i = 0; i < fdCount; ++i)
    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL | POLLRDHUP)) {
      Emu *emu = emusToCheck[i];
      syslog(LOG_ERR, "poll failed because revents=0x%x for `%s`.", fds[i].revents, i == 0 ? "xenopsd" : emu->name);
      EmuError = EINVAL;
      if (i > 0)
        emu_handle_error(emu, EmuError, "wait_for_event");
      return -1;
    }

  // 3. Process control in.
  if ((fds[0].revents & POLLIN)) {
    fds[0].revents = 0;
    if (control_receive_and_process_messages(0) < 0)
      return -1;
  }

  // 4. Process emus.
  for (uint i = 1; i < fdCount; ++i) {
    if (!(fds[i].revents & POLLIN)) continue;

    Emu *emu = emusToCheck[i];
    const int ret = emu_client_receive_events(emu->client, 0);
    if (ret < 0) {
      if (EmuError == EPIPE) {
        emu->client->fd = -1;
        emu_handle_error(emu, EmuErrorDisconnected, "emu_client_receive_events");
        return -1;
      }
      emu_handle_error(emu, EmuError, "emu_client_receive_events");
      return -1;
    }

    if (emu_client_process_events(emu->client) < 0) {
      emu_handle_error(emu, EmuError, "emu_client_process_events");
      return -1;
    }
  }

  return 0;
}

static int emu_manager_process (bool (*cb)(Emu *emu)) {
  for (;;) {
    // 1. Check if the condition is valid.
    bool process = false;
    Emu *emu;
    foreach (emu, Emus)
      if ((process = (*cb)(emu)))
        break;
    if (!process) break; // Nothing to do.

    // 2. Condition is not valid. Poll and compute.
    if (emu_manager_poll() < 0)
      if (EmuError) {
        if (EmuError == ETIME) {
          syslog(LOG_DEBUG, "Get ETIME when waiting for events.");
        } else if (EmuError == ESHUTDOWN) {
          return -1;
        } else {
          syslog(LOG_ERR, "Error waiting for events: `%s`.", strerror(EmuError));
          return -1;
        }
      }

    if (emu_manager_send_progress() < 0)
      return -1;
  }

  return 0;
}

// -----------------------------------------------------------------------------
// Subphases used by emu_manager_save.
// -----------------------------------------------------------------------------

static inline int emu_manager_request_track () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus) {
    if (!emu->flags)
      continue;

    if (emu->type == EmuTypeEmp) {
      if (emu_client_send_emp_cmd(emu->client, cmd_track_dirty, NULL) < 0)
        return -1;
      if (emu_client_send_emp_cmd(emu->client, cmd_migrate_progress, NULL) < 0)
        return -1;
    } else if (emu->type == EmuTypeQmpLibxl) {
      ArgNode node = { NULL, "enable", "true" };
      if (emu_client_send_qmp_cmd(emu->client, QmpCommandNumXenSetGlobalDirtyLog, &node) < 0)
        return -1;
      if (emu_disconnect(emu) < 0)
        return -1;
    }
  }

  return 0;
}

static inline int emu_manager_migrate_live () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus) {
    if (!(emu->flags & EMU_FLAG_MIGRATE_LIVE))
      continue; // Nothing to do is current emu does not support live migration.

    if (emu_set_stream_busy(emu, true) < 0)
      return -1;

    if (control_send_prepare(emu->name) < 0) {
      if (EmuError != ESHUTDOWN)
        syslog(LOG_ERR, "Failed to prepare stream for `%s`: `%s`.", emu->name, strerror(EmuError));
      return -1;
    }

    if (emu_client_send_emp_cmd(emu->client, cmd_migrate_live, NULL) < 0)
      return -1;
  }

  return 0;
}

static inline int emu_manager_wait_live_stage_done () {
  EMU_LOG_PHASE();
  return emu_manager_process(emu_process_cb_wait_live_stage_done);
}

static inline int emu_manager_migrate_paused () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus)
    if ((emu->flags & EMU_FLAG_MIGRATE_PAUSED) && emu_client_send_emp_cmd(emu->client, cmd_migrate_pause, NULL) < 0)
      return -1;

  foreach (emu, Emus)
    if ((emu->flags & EMU_FLAG_MIGRATE_PAUSED) && emu_client_send_emp_cmd(emu->client, cmd_migrate_paused, NULL) < 0)
      return -1;

  return 0;
}

static inline int emu_manager_wait_migrate_live_finished () {
  EMU_LOG_PHASE();
  return emu_manager_process(emu_process_cb_wait_migrate_live_finished);
}

static inline int emu_manager_migrate_non_live () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus) {
    if (!(emu->flags & EMU_FLAG_MIGRATE_NON_LIVE))
      continue;

    if (
      emu_set_stream_busy(emu, true) < 0 ||
      control_send_prepare(emu->name) < 0 ||
      emu_client_send_emp_cmd(emu->client, cmd_migrate_nonlive, NULL) < 0
    )
      return -1;

    while (emu->state != EMU_STATE_MIGRATION_DONE) {
      if (emu_manager_poll() < 0 && EmuError != ETIME) {
        syslog(LOG_ERR, "Error waiting for events: `%s`.", strerror(EmuError));
        return -1;
      }
      if (emu_manager_send_progress() < 0)
        return -1;
    }
  }

  return 0;
}

// -----------------------------------------------------------------------------

int emu_manager_configure (bool live, EmuMode mode) {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus) {
    // Close automatically fd stream before call to emu_manager_fork.
    if (emu->stream && xcp_fd_set_close_on_exec(emu->stream->fd, true) != XCP_ERR_OK) {
      syslog(LOG_ERR, "Failed to set_cloexec flag on stream %d for `%s`: `%s`.", emu->stream->fd, emu->name, strerror(errno));
      EmuError = errno;
      return -1;
    }

    if (!(emu->flags & EMU_FLAG_ENABLED)) {
      emu->flags = 0; // Reset all flags. Must be easy to check instead of: `flags & EMU_FLAG_ENABLED`.
      continue;
    }
    syslog(LOG_INFO, "Emu `%s` is enabled.", emu->name);

    if (emu->type == EmuTypeEmp) {
      if (!live) {
        emu->flags &= ~(EMU_FLAG_MIGRATE_LIVE | EMU_FLAG_WAIT_LIVE_STAGE_DONE);
        emu->flags |= EMU_FLAG_MIGRATE_NON_LIVE;
      }
    } else if (emu->type == EmuTypeQmpLibxl && (!live || mode == EmuModeHvmRestore || mode == EmuModeRestore))
      emu->flags = 0; // Disable QMP emu because it is unused in restore mode.
  }

  return 0;
}

int emu_manager_fork (uint domId) {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus)
  if (emu->pathName && emu->type == EmuTypeEmp && emu_fork_emp_client(emu, domId) < 0)
    return -1;
  return 0;
}

int emu_manager_connect (uint domId) {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus)
    if (emu_connect(emu, domId) < 0)
      return -1;
  return 0;
}

int emu_manager_disconnect () {
  EMU_LOG_PHASE();

  int ret = 0;
  Emu *emu;
  foreach(emu, Emus)
    if (emu_disconnect(emu) < 0)
      ret = -1;
  return ret;
}

int emu_manager_init () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus)
    if (emu_init(emu) < 0)
      return -1;
  return 0;
}

int emu_manager_wait_termination () {
  EMU_LOG_PHASE();

  uint nChildrenToWait = 0;
  Emu *emu;
  foreach (emu, Emus)
    if (emu->pathName && emu->pid)
      ++nChildrenToWait;

  struct sigaction sigact = { .sa_handler = emu_manager_termination_timeout_handler };
  sigemptyset(&sigact.sa_mask);
  if (sigaction(SIGALRM, &sigact, 0) < 0) {
    syslog(LOG_ERR, "Failed to ignore SIGALRM: `%s`.", strerror(errno));
    return -1;
  }

  WaitEmusTermination = true;

  alarm(60);

  syslog(LOG_DEBUG, "Children to wait: %d.", nChildrenToWait);
  while (WaitEmusTermination && nChildrenToWait) {
    syslog(LOG_DEBUG, "Waiting for children.");

    int status;
    pid_t pid = wait(&status);

    Emu *terminatedEmu = NULL;
    foreach (emu, Emus)
      if (emu->pid == pid) {
        terminatedEmu = emu;
        break;
      }
    if (!terminatedEmu)
      continue;

    if (WIFEXITED(status)) {
      const int code = WEXITSTATUS(status);
      if (WEXITSTATUS(status) == 0)
        syslog(LOG_INFO, "Emu `%s` %s.", terminatedEmu->name, "completed normally");
      else {
        syslog(LOG_ERR, "Emu `%s` %s: %d.", terminatedEmu->name, "exited with an error", code);
        if (!terminatedEmu->errorCode) terminatedEmu->errorCode = EmuErrorExitedWithErr;
      }
    } else if (WIFSIGNALED(status)) {
      syslog(LOG_ERR, "Child `%s` terminated by signal %d.", terminatedEmu->name, WTERMSIG(status));
      if (!terminatedEmu->errorCode) terminatedEmu->errorCode = EmuErrorKilled;
    }

    --nChildrenToWait;
    terminatedEmu->pid = 0;
  }

  alarm(0);
  if (!WaitEmusTermination)
    syslog(LOG_ERR, "Timeout on emu exit.");

  foreach (emu, Emus)
    if (emu->pathName && emu->pid) {
      syslog(LOG_ERR, "Sending sigkill to `%s`...", emu->name);
      kill(emu->pid, SIGKILL);
      for (;;) {
        if (waitpid(emu->pid, NULL, 0) < 0) {
          if (errno == EINTR) continue;
          syslog(LOG_ERR, "Failed to wait for `%s`: `%s`.", emu->name, strerror(errno));
        }
        break;
      }
    }

  syslog(LOG_DEBUG, "All children exited!");

  return 0;
}

int emu_manager_clean () {
  EMU_LOG_PHASE();

  Emu *emu;
  foreach (emu, Emus) {
    arg_list_free(emu->arguments);
    emu->arguments = NULL;

    free(emu->progress.result);
    emu->progress.result = NULL;
  }
  return 0;
}

// -----------------------------------------------------------------------------

int emu_manager_restore () {
  EMU_LOG_PHASE();

  uint nEmuToWait = 0;
  Emu *emu;
  foreach (emu, Emus)
    if (emu->flags)
      ++nEmuToWait;

  while (nEmuToWait) {
    if (emu_manager_poll() < 0 && EmuError && EmuError != ETIME) {
      if (EmuError != ESHUTDOWN)
        syslog(LOG_ERR, "Error waiting for events: `%s`.", strerror(EmuError));
      return -1;
    }

    foreach (emu, Emus) {
      if (emu->state != EMU_STATE_MIGRATION_DONE)
        continue;

      if (control_send_result(emu->name, emu->progress.result) < 0)
        return -1;

      emu->state = EMU_STATE_COMPLETED;
      --nEmuToWait;
    }
  }

  return 0;
}

int emu_manager_save (bool live) {
  EMU_LOG_PHASE();

  // 1. Trying to copy a lot of dirty pages in the first Xen iterations.
  if (live && (
    emu_manager_request_track() < 0 ||
    emu_manager_migrate_live() < 0 ||
    emu_manager_wait_live_stage_done() < 0
  ))
    goto fail;

  // 2. Suspend and copy the remaining dirty RAM pages in the last iteration.
  if (
    control_send_suspend() < 0 ||
    emu_manager_migrate_paused() < 0 ||
    emu_manager_wait_migrate_live_finished() < 0
  )
    goto fail;

  // 3. Migrate emus without live mode. So... No iteration.
  if (emu_manager_migrate_non_live() < 0)
    goto fail;

  // 4. Send final migration result to xenopsd.
  if (control_send_final_result() < 0)
    goto fail;

  return 0;

fail:
  {
    // Cache first error.
    int error = EmuError;
    emu_manager_abort_save();
    EmuError = error;
  }
  return -1;
}

int emu_manager_abort_save () {
  int error = 0;

  Emu *emu;
  foreach (emu, Emus) {
    if (
      emu->flags &&
      emu->type == EmuTypeEmp &&
      emu->client &&
      emu->client->fd > -1 &&
      emu_client_send_emp_cmd(emu->client, cmd_migrate_abort, NULL) < 0
    ) {
      syslog(LOG_ERR, "Failed to call cmd_migrate_abort: `%s`.", strerror(EmuError));
      if (!error)
        error = EmuError;
    }
  }

  if (error) {
    EmuError = error;
    return -1;
  }
  return 0;
}

// -----------------------------------------------------------------------------

Emu *emu_manager_find_first_failed () {
  Emu *emu;
  foreach (emu, Emus)
    if (emu->isFirstFailedEmu)
      return emu;
  return NULL;
}
