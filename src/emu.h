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

// Like a errno variable but used for emu errors.
extern __thread int EmuError;

// =============================================================================
// Emu flags.
// =============================================================================

// See: https://github.com/xcp-ng-rpms/libempserver (emp.h)

// Should we use this emu during the migration process or not?
#define EMU_FLAG_ENABLED (1 << 0)

// Emu accept live migration, i.e.: Start migration while guest is still running.
#define EMU_FLAG_MIGRATE_LIVE (1 << 1)

// It's necessary to wait emu after live migration success.
// Live stage is done when it remains very few dirty pages to move.
#define EMU_FLAG_WAIT_LIVE_STAGE_DONE (1 << 2)

// Request the pause status for one emu. I.e. no data can be written by this emu.
// Must be used after a migrate live call to migrate the remaining dirty pages.
#define EMU_FLAG_MIGRATE_PAUSED (1 << 3)

// Emu is migrated directly. More violent than live mode.
#define EMU_FLAG_MIGRATE_NON_LIVE (1 << 5)

// =============================================================================
// Emu states.
// =============================================================================

// Emu must be initialized. It depends of the emu type like a QMP connection.
#define EMU_STATE_UNINITIALIZED 0

// Emu is initialized. :)
#define EMU_STATE_INITIALIZED 1

// Emu is being restoring...
#define EMU_STATE_RESTORING 2

// Live stage is done. => We can set the pause status on this emu and migrate the remaining dirty pages.
#define EMU_STATE_LIVE_STAGE_DONE 3

// Migration is a success. \o/
#define EMU_STATE_MIGRATION_DONE 4

// Nothing to do after that.
#define EMU_STATE_COMPLETED 5

// =============================================================================
// Emu.
// =============================================================================

typedef enum EmuType {
  EmuTypeEmp,
  EmuTypeQmpLibxl
} EmuType;

typedef enum EmuMode {
  EmuModeHvmSave,
  EmuModeSave,
  EmuModeHvmRestore,
  EmuModeRestore
} EmuMode;

typedef enum EmuErrorOffset {
  EmuErrorDisconnected = -2,
  EmuErrorKilled = -3,
  EmuErrorExitedWithErr = -4
} EmuErrorOffset;

// -----------------------------------------------------------------------------

typedef struct ArgNode ArgNode;
typedef struct EmuClient EmuClient;
typedef struct EmuStream EmuStream;

// -----------------------------------------------------------------------------

// Used by source emu-manager when RAM data is transferred.
typedef struct EmuMigrationProgress {
  char *result; // Result to send via xenopsd. (Progress bar)

  // Data (RAM) sent and remaining data.
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

// -----------------------------------------------------------------------------

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

  EmuMigrationProgress progress;

  // Only used by QMP libxl emu.
  bool qmpConnectionEstablished;
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

int emu_manager_configure (bool live, EmuMode mode);

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
