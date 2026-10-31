# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)

# Builds the LXR sound engine for a desktop computer (tested with gcc on Linux x86-64).
# This is a separate project from the firmware in the parent directory:
#
#   cmake -S host -B build/host
#   cmake --build build/host
#   build/host/lxr_render kit.wav

project(LXR_Host LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LXR_HOST_SANITIZE "Build with address and undefined behaviour sanitizers" OFF)

set(LXR_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../Core)

# The engine runs at its own fixed rate; libsamplerate converts to the host rate. An installed copy is used if
# found; set FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER to build the pinned version statically instead.
include(FetchContent)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SampleRate
    GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
    GIT_TAG 0.2.2
    FIND_PACKAGE_ARGS CONFIG
)
FetchContent_MakeAvailable(SampleRate)

file(GLOB_RECURSE LXR_DSP_SOURCES CONFIGURE_DEPENDS ${LXR_CORE}/Src/Soundengine/DSPAudio/*.c)

add_library(lxr_engine STATIC
    ${LXR_DSP_SOURCES}
    ${LXR_CORE}/Src/Soundengine/FxBus.c
    ${LXR_CORE}/Src/Soundengine/MIDI/MidiParser.c
    ${LXR_CORE}/Src/Soundengine/MIDI/ParameterArray.c
    ${LXR_CORE}/Src/Soundengine/MIDI/MidiVoiceControl.c
    ${LXR_CORE}/Src/Soundengine/MIDI/MidiMessages.c
    ${LXR_CORE}/Src/Parameters.c
    ${LXR_CORE}/Src/Preset/initKit.c
    src/lxr_host.c
    src/stubs.c
)
# shim/ stands in for the STM32 HAL and must come before the firmware headers
target_include_directories(lxr_engine PUBLIC shim ${LXR_CORE}/Inc src)
# -fcommon: the firmware defines some globals in headers
# -w: the firmware isn't warning-clean and the noise would bury host-side warnings
target_compile_options(lxr_engine PRIVATE -fcommon -w)
target_link_libraries(lxr_engine PUBLIC m PRIVATE SampleRate::samplerate)

if(LXR_HOST_SANITIZE)
    target_compile_options(lxr_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(lxr_engine PUBLIC -fsanitize=address,undefined)
endif()

add_executable(lxr_render src/lxr_render.c)
target_link_libraries(lxr_render PRIVATE lxr_engine)
