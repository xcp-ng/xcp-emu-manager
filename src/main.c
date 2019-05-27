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
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include <xcp-ng/generic.h>

#include "arg-list.h"
#include "control.h"
#include "emu.h"

// =============================================================================

static void usage (const char *progname) {
  printf("Usage: %s [OPTIONS]\n", progname);
  puts("  --domid                  domain ID");
  puts("  --fd                     data descriptor");
  puts("  --controlinfd            control input descriptor");
  puts("  --controloutfd           control output descriptor");
  puts("  --store_port             store port");
  puts("  --console_port           console port");
  puts("  --live                   enable live migration");
  puts("  --mode                   migration mode");
  puts("  --dm                     device model");
  puts("  --debug                  enable debug logs");
  puts("  --help                   print this help and exit");
}

// -----------------------------------------------------------------------------

#define MAIN_OPT_DEBUG 1
#define MAIN_OPT_DEVICE_MODEL 2
#define MAIN_OPT_FORK 3

int main (int argc, char *argv[]) {
  openlog(argv[0], LOG_PID, LOG_USER | LOG_MAIL);

  const struct option longopts[] = {
    { "domid", 1, NULL, 'd' },
    { "fd", 1, NULL, 'f' },
    { "controlinfd", 1, NULL, 'i' },
    { "controloutfd", 1, NULL, 'o' },
    { "store_port", 1, NULL, 's' },
    { "console_port", 1, NULL, 'c' },
    { "live", 1, NULL, 'l' },
    { "mode", 1, NULL, 'm' },
    { "dm", 1, NULL, MAIN_OPT_DEVICE_MODEL },
    { "fork", 1, NULL, MAIN_OPT_FORK },
    { "debug", 0, NULL, MAIN_OPT_DEBUG },
    { "help", 0, NULL, 'h' },
    { NULL, 0, 0, 0 }
  };

  const char *modes[] = {
    "hvm_save",
    "save",
    "hvm_restore",
    "restore"
  };

  Emu *xenguestEmu = emu_from_name("xenguest");
  assert(xenguestEmu);

  // 1. Parse arguments.
  int mode = -1;

  bool soFarSoGood;

  uint domId = (uint)-1;
  int controlInFd = -1;
  int controlOutFd = -1;
  bool live = false;

  bool debugMode = false;
  #ifdef DEBUG
    debugMode = true;
    syslog(LOG_DEBUG, "Force debug mode! (Binary compiled with debug flags.)");
  #endif // ifdef DEBUG

  int option;
  int longindex = 0;
  while ((option = getopt_long_only(argc, argv, "", longopts, &longindex)) != -1) {
    switch (option) {
      case 'd':
        domId = (uint)xcp_str_to_int(optarg, &soFarSoGood);
        if (!soFarSoGood) {
          syslog(LOG_ERR, "Unable to convert domId to int.");
          return EXIT_FAILURE;
        }
        break;
      case 'f': {
        const int fd = xcp_str_to_int(optarg, &soFarSoGood);
        if (!soFarSoGood || fd <= -1) {
          syslog(LOG_ERR, "Unable to convert fd to int. It must be positive or 0.");
          return EXIT_FAILURE;
        }
        if (emu_create_stream(xenguestEmu, fd) < 0)
          return EXIT_FAILURE;
      } break;
      case 'i':
        controlInFd = xcp_str_to_int(optarg, &soFarSoGood);
        if (!soFarSoGood) {
          syslog(LOG_ERR, "Unable to convert control in fd to int.");
          return EXIT_FAILURE;
        }
        break;
      case 'o':
        controlOutFd = xcp_str_to_int(optarg, &soFarSoGood);
        if (!soFarSoGood) {
          syslog(LOG_ERR, "Unable to convert control out fd to int.");
          return EXIT_FAILURE;
        }
        break;
      case 's':
        if (arg_list_append_str(&xenguestEmu->arguments, "store_port", optarg) < 0) {
          syslog(LOG_ERR, "Failed to add store_port argument: `%s`.", strerror(errno));
          return EXIT_FAILURE;
        }
        break;
      case 'c':
        if (arg_list_append_str(&xenguestEmu->arguments, "console_port", optarg) < 0) {
          syslog(LOG_ERR, "Failed to add console_port argument: `%s`.", strerror(errno));
          return EXIT_FAILURE;
        }
        break;
      case 'l':
        if (!strcmp(optarg, "true"))
          live = true;
        else if (!strcmp(optarg, "false"))
          live = false;
        else {
          syslog(LOG_ERR, "Unable to set live argument to unknown value: `%s`. Supported: [true, false].", optarg);
          return EXIT_FAILURE;
        }
        break;
      case 'm':
        if ((mode = (int)xcp_str_arr_index_of(modes, XCP_ARRAY_LEN(modes), optarg)) == -1) {
          syslog(LOG_ERR, "Unknown mode: `%s`.", optarg);
          return EXIT_FAILURE;
        }
        break;
      case MAIN_OPT_DEVICE_MODEL: {
        char *fdStr = strchr(optarg, ':');
        if (fdStr)
          *fdStr++ = 0;

        Emu *emu = emu_from_name(optarg);
        if (!emu) {
          syslog(LOG_ERR, "Bad dm: `%s`:`%s`", optarg, fdStr);
          return EXIT_FAILURE;
        }

        emu->flags |= EMU_FLAG_ENABLED;
        if (!fdStr) continue;

        if (emu->type == EmuTypeQmpLibxl) {
          syslog(LOG_ERR, "Cannot create stream on emu `%s`. Unsupported operation.", emu->name);
          return EXIT_FAILURE;
        }

        const int fd = xcp_str_to_int(fdStr, &soFarSoGood);
        if (!soFarSoGood || fd <= -1) {
          syslog(LOG_ERR, "Unable to convert dm to int. It must be positive or 0.");
          return EXIT_FAILURE;
        }
        if (emu_create_stream(emu, fd) < 0)
          return EXIT_FAILURE;
      } break;
      case MAIN_OPT_FORK:
        // TODO: Find the fork usage?
        syslog(LOG_INFO, "Called with fork argument: `--fork %s`.", optarg);
        break;
      case MAIN_OPT_DEBUG:
        debugMode = true;
        break;
      case 'h':
        usage(argv[0]);
        return EXIT_SUCCESS;
      case '?':
        if (optopt == 0)
          syslog(LOG_ERR, "Unknown option: `%s`.", argv[optind - 1]);
        else
          syslog(LOG_ERR, "Error parsing option: `-%c`", optopt);
        syslog(LOG_ERR, "Try `%s --help` for more information.", *argv);
        return EXIT_FAILURE;
    }
  }

  // 2. Checking and using arguments as config.
  if (mode == -1) {
    syslog(LOG_ERR, "Operation mode is not set!");
    return EXIT_FAILURE;
  }

  if (controlInFd == -1 || controlOutFd == -1) {
    syslog(LOG_ERR, "Control fd(s) not set!");
    return EXIT_FAILURE;
  }

  if (domId == (uint)-1) {
    syslog(LOG_ERR, "Domid not set!");
    return EXIT_FAILURE;
  }

  // 3. Open system logger with explicit progname.
  char progname[256];
  if (snprintf(progname, sizeof progname, "%s-%u", basename(*argv), domId) < 0) {
    syslog(LOG_ERR, "Failed to set progname: `%s`.", strerror(errno));
    return EXIT_FAILURE;
  }
  openlog(progname, LOG_PID, LOG_USER | LOG_MAIL);
  setlogmask(LOG_UPTO(debugMode ? LOG_DEBUG : LOG_INFO));

  // 4. Ignore SIGPIPE.
  struct sigaction sigact = { .sa_handler = SIG_IGN };
  sigemptyset(&sigact.sa_mask);
  if (sigaction(SIGPIPE, &sigact, 0) < 0) {
    syslog(LOG_ERR, "Failed to ignore SIGPIPE: `%s`.", strerror(errno));
    return EXIT_FAILURE;
  }

  // 5. Start restore or save.
  if (
    xcp_fd_set_close_on_exec(controlInFd, true) != XCP_ERR_OK ||
    xcp_fd_set_close_on_exec(controlOutFd, true) != XCP_ERR_OK
  ) {
    syslog(LOG_ERR, "Failed to set_cloexec flag for control fds: `%s`.", strerror(errno));
    return EXIT_FAILURE;
  }

  syslog(LOG_INFO, "Startup: xenopsd control fds (%d, %d).", controlInFd, controlOutFd);
  syslog(LOG_INFO, "Startup: domid %u.", domId);
  syslog(LOG_INFO, "Startup: operation mode (%s, %s).", modes[mode], live ? "live" : "non-live");

  syslog(LOG_DEBUG, "Configuring xenopsd...");
  if (control_init(controlInFd, controlOutFd) < 0)
    return EXIT_FAILURE;

  if (
    (mode == EmuModeSave || mode == EmuModeRestore) &&
    arg_list_append_str(&xenguestEmu->arguments, "pv", "true") < 0
  ) {
    syslog(LOG_ERR, "Failed to add pv argument: `%s`.", strerror(errno));
    return EXIT_FAILURE;
  }

  int error = 0;

  if (
    emu_manager_configure(live, mode) < 0 ||
    emu_manager_fork(domId) < 0 ||
    emu_manager_connect(domId) < 0 ||
    emu_manager_init() < 0
  )
    goto fail;

  if (
    (mode == EmuModeHvmRestore || mode == EmuModeRestore)
      ? emu_manager_restore() < 0
      : emu_manager_save(live) < 0
  )
    goto fail;

  // Success. \o/
  goto end;

fail:
  error = EmuError;

end:
  if (emu_manager_disconnect() < 0 && !error)
    error = EmuError; // Update error only if there is no previous error!

  // Ignore errors of emu_manager_wait_termination.
  emu_manager_wait_termination();
  emu_manager_clean();

  if (error == ESHUTDOWN || !error)
    return EXIT_SUCCESS;

  control_report_error(error);
  return EXIT_FAILURE;
}
