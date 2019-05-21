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

#include <xcp-ng/generic.h>

#include "qmp.h"

// =============================================================================

const char *qmp_command_from_num (QmpCommandNum num) {
  static const char *commands[] = {
    "qmp_capabilities",
    "xen-set-global-dirty-log"
  };
  assert(num >= 0 && num < XCP_ARRAY_LEN(commands));
  return commands[num];
}
