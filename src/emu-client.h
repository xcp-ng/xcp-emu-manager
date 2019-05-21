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

#ifndef _EMU_CLIENT_H_
#define _EMU_CLIENT_H_

#include <emp.h>
#include <json-c/json.h>

#include "qmp.h"

// =============================================================================

typedef struct ArgNode ArgNode;
typedef struct Emu Emu;
typedef struct EmuClient EmuClient;

// TODO: Add a Emp prefix in the emp library.
typedef enum command_num EmpCommandNum;

typedef int (*EmuClientCb)(EmuClient *client, const char *eventType, const json_object *obj);

typedef struct EmuClient {
  Emu *emu;

  char buf[1024];
  size_t bufSize;

  int fd;
  bool waitingAck;
  json_tokener *tokener;
  EmuClientCb eventCb;
} EmuClient;

// -----------------------------------------------------------------------------

int emu_client_create (EmuClient **client, EmuClientCb eventCb, Emu *emu);
int emu_client_destroy (EmuClient *client);

int emu_client_connect (EmuClient *client, const char *path);

int emu_client_receive_events (EmuClient *client, int timeout);

int emu_client_process_events (EmuClient *client);

int emu_client_send_emp_cmd (EmuClient *client, EmpCommandNum cmdNum, const ArgNode *arguments);
int emu_client_send_emp_cmd_with_fd (EmuClient *client, EmpCommandNum cmdNum, int fd, const ArgNode *arguments);
int emu_client_send_qmp_cmd (EmuClient *client, QmpCommandNum cmdNum, const ArgNode *arguments);

#endif // ifndef _EMU_CLIENT_H_
