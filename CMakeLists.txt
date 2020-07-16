# ==============================================================================
# CMakeLists.txt
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

cmake_minimum_required(VERSION 3.13.1)

# ------------------------------------------------------------------------------
# Config & flags.
# ------------------------------------------------------------------------------

project(xcp-emu-manager VERSION 1.1.3 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

set(XCP_EMU_MANAGER_BIN emu-manager)

set(CUSTOM_C_FLAGS
  -Wall
  -Wcast-align
  -Wconversion
  -Werror
  -Wextra
  -Wfloat-equal
  -Wformat-nonliteral
  -Wpointer-arith
  -Wsign-conversion
)

if (CMAKE_C_COMPILER_ID MATCHES "GNU")
  list(APPEND CUSTOM_C_FLAGS
    -Wlogical-op
  )
endif ()

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

find_package(Emp REQUIRED)
find_package(JsonC REQUIRED)
find_package(XcpNgGeneric 1.1.0 REQUIRED)

set(LIBS
  Emp::Emp
  JsonC::JsonC
  XcpNg::Generic
)

# ------------------------------------------------------------------------------
# Sources.
# ------------------------------------------------------------------------------

set(SOURCES
  src/arg-list.c
  src/control.c
  src/emu-client.c
  src/emu.c
  src/main.c
  src/qmp.c
)

# ------------------------------------------------------------------------------
# Binary.
# ------------------------------------------------------------------------------

add_compile_options(${CUSTOM_C_FLAGS})

add_executable(${XCP_EMU_MANAGER_BIN} ${SOURCES})
set_property(TARGET ${XCP_EMU_MANAGER_BIN} PROPERTY LINKER_LANGUAGE C)

target_link_libraries(${XCP_EMU_MANAGER_BIN} PRIVATE ${LIBS})

include(GNUInstallDirs)

install(TARGETS ${XCP_EMU_MANAGER_BIN}
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/xen/bin
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
