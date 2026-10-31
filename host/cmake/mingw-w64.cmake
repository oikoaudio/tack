# SPDX-License-Identifier: GPL-3.0-only
#
# Cross-compiles for 64-bit Windows with mingw-w64 (gcc), e.g. from Linux:
#
#   cmake -S host -B build/win -G Ninja -DCMAKE_TOOLCHAIN_FILE=host/cmake/mingw-w64.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# run the tools that are built, e.g. for tests, through Wine
set(CMAKE_CROSSCOMPILING_EMULATOR wine)
