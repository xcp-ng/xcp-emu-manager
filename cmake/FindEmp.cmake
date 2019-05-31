# ==============================================================================
# FindEmp.cmake
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

# Find the emp library.
#
# This will define the following variables:
#   Emp_FOUND
#   Emp_INCLUDE_DIRS
#   Emp_LIBRARIES
#
# and the following imported targets:
#   Emp::Emp

find_path(Emp_INCLUDE_DIR
  NAMES emp.h
  PATHS /usr/include
)

find_library(Emp_LIBRARY
  NAMES empserver
  PATHS /usr/lib /usr/lib64
)

mark_as_advanced(Emp_FOUND Emp_INCLUDE_DIR Emp_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Emp
  REQUIRED_VARS Emp_INCLUDE_DIR Emp_LIBRARY
)

if (Emp_FOUND)
  get_filename_component(Emp_INCLUDE_DIRS ${Emp_INCLUDE_DIR} DIRECTORY)
endif ()
set(Emp_LIBRARIES ${Emp_LIBRARY})

if (Emp_FOUND AND NOT TARGET Emp::Emp)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)

  add_library(Emp::Emp INTERFACE IMPORTED)
  set_target_properties(Emp::Emp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${Emp_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${Emp_LIBRARIES};Threads::Threads"
  )
endif ()
