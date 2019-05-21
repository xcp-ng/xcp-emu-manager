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

#ifndef _EMU_H_
#define _EMU_H_

#include <stdbool.h>
#include <sys/types.h>

// =============================================================================

extern __thread int EmuError;

// =============================================================================
// Emu.
// =============================================================================

enum EmuType {
  EmuTypeEmp,
  EmuTypeQmpLibxl
};

enum EmuMode {
  EmuModeHvmSave,
  EmuModeSave,
  EmuModeHvmRestore,
  EmuModeRestore
};

enum EmuErrorOffset {
  EmuErrorDisconnected = -2,
  EmuErrorKilled = -3,
  EmuErrorExitedWithErr = -4
};

// TODO: Find better names and complete.
#define EMU_FLAG_ENABLED (1 << 0)
#define EMU_FLAG_LIVE (1 << 1)
#define EMU_FLAG_FIND_NAME (1 << 2)
#define EMU_FLAG_PAUSE (1 << 3)
#define EMU_FLAG_MIGRATE_PAUSED (1 << 4)
#define EMU_FLAG_NON_LIVE (1 << 5)

// TODO: Find better names and complete.
#define EMU_STATE_UNINITIALIZED 0
#define EMU_STATE_INITIALIZED 1
#define EMU_STATE_RESTORING 2
#define EMU_STATE_LIVE_STAGE_DONE 3
#define EMU_STATE_RESULT_AVAILABLE 4
#define EMU_STATE_COMPLETED 5

typedef struct ArgNode ArgNode;
typedef struct EmuClient EmuClient;
typedef struct EmuStream EmuStream;

// Used by source emu-manager when RAM data is transferred.
typedef struct EmuMigrationProgress {
  char *eventResult;
  int64_t remaining;
  int64_t sent;

  // See: tools/libxc/xc_sr_save.c in Xen.
  // And: https://www.gta.ufrj.br/ftp/gta/TechReports/PMC10.pdf
  // And: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4938839/
  // And: https://www.usenix.org/legacy/event/nsdi05/tech/full_papers/clark/clark.pdf
  int iteration;

  // Used to smooth progress report when remaining is unknown.
  // See: https://github.com/xcp-ng-rpms/libempserver (emp.h => mid iteration)
  int64_t sentMidIteration;

  // When iteration is not known, it's necessary to use a fake emu size.
  int64_t fakeTotal;
} EmuMigrationProgress;

typedef struct Emu {
  const char *name;
  const char *pathName;
  pid_t pid;
  int type;
  int flags;
  EmuClient *client;
  EmuStream *stream;
  int state;

  int errorCode;
  bool isFirstFailedEmu;
  ArgNode *arguments;

  // TODO: Maybe use a union of two structs: for emp and qmp.
  bool qmpConnectionEstablished;

  EmuMigrationProgress progress;
} Emu;

// -----------------------------------------------------------------------------

const char *emu_error_code_to_str (int errorCode);
Emu *emu_from_name (const char *name);

// =============================================================================
// EmuStream.
// =============================================================================

typedef struct Emu Emu;

int emu_create_stream (Emu *emu, int fd);
int emu_set_stream_busy (Emu *emu, bool status);

// =============================================================================
// EmuManager.
// =============================================================================

// TODO: Add an emu_manager_start (bool live, int mode, uint domId)
// and remove public fonctions after this point.
int emu_manager_configure (bool live, int mode);

int emu_manager_fork (uint domId);

int emu_manager_connect (uint domId);

int emu_manager_disconnect ();

int emu_manager_init ();

int emu_manager_wait_termination ();

int emu_manager_clean ();

int emu_manager_restore ();
int emu_manager_save (bool live);

Emu *emu_manager_find_first_failed ();

#endif // ifndef _EMU_H_
