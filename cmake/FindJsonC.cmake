# ==============================================================================
# FindJsonC.cmake
#
# Copyright (C) 2019  xcp-emu-manager
# Copyright (C) 2019  Vates SAS
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
# ==============================================================================

# Find the json-c library.
#
# This will define the following variables:
#   JSON_C_FOUND
#   JSON_C_VERSION
#   JSON_C_INCLUDE_DIRS
#   JSON_C_LIBRARIES
#
# and the following imported targets:
#   JsonC::JsonC

find_package(PkgConfig)
pkg_check_modules(PC_JSON_C QUIET json-c json)

find_path(JSON_C_INCLUDE_DIR
  NAMES json.h
  HINTS ${PC_JSON_C_INCLUDE_DIRS}
  PATH_SUFFIXES json-c json
)

find_library(JSON_C_LIBRARY
  NAMES json-c libjson-c
  HINTS ${PC_JSON_C_LIBRARY_DIRS}
)

set(JSON_C_VERSION ${PC_JSON_C_VERSION})

mark_as_advanced(JSON_C_FOUND JSON_C_VERSION JSON_C_INCLUDE_DIR JSON_C_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JSON_C
  REQUIRED_VARS JSON_C_INCLUDE_DIR JSON_C_LIBRARY
  VERSION_VAR JSON_C_VERSION
)

if (JSON_C_FOUND)
  get_filename_component(JSON_C_INCLUDE_DIRS ${JSON_C_INCLUDE_DIR} DIRECTORY)
endif ()
set(JSON_C_LIBRARIES ${JSON_C_LIBRARY})

if (JSON_C_FOUND AND NOT TARGET JsonC::JsonC)
  add_library(JsonC::JsonC INTERFACE IMPORTED)
  set_target_properties(JsonC::JsonC PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${JSON_C_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${JSON_C_LIBRARIES}"
  )
endif ()
