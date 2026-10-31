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
option(LXR_HOST_CLAP "Build the Tack CLAP plugin" ON)

# everything ends up in a plugin that shares a process with other plugins: keep the engine's globals private
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)

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

# Parameter names, types and ranges come from the front panel's menu tables. valueNames and parameter_dtypes sit in
# menu.c among LCD code that doesn't build on a desktop, so copy just those two definitions out of it.
set(LXR_MENU_C ${LXR_CORE}/Src/menu/menu.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${LXR_MENU_C})
file(READ ${LXR_MENU_C} menu_src)
string(REGEX MATCH "const Name valueNames\\[[A-Z_]+\\][^;]*;" menu_value_names "${menu_src}")
string(REGEX MATCH "const enum Datatypes PROGMEM parameter_dtypes\\[[A-Z_]+\\][^;]*;" menu_dtypes "${menu_src}")
if(NOT menu_value_names OR NOT menu_dtypes)
    message(FATAL_ERROR "Could not find valueNames and parameter_dtypes in ${LXR_MENU_C}")
endif()
set(LXR_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(CONFIGURE OUTPUT ${LXR_GENERATED}/menu_tables.inc
    CONTENT "// Generated from Core/Src/menu/menu.c by host/CMakeLists.txt. Do not edit.\n\n${menu_value_names}\n\n${menu_dtypes}\n"
    @ONLY)

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
    # modTargets: the list of valid modulation targets, used by the kit loader
    ${LXR_CORE}/Src/menu/Cc2Text.c
    src/lxr_host.c
    src/lxr_params.c
    src/stubs.c
)
# shim/ stands in for the STM32 HAL and must come before the firmware headers
target_include_directories(lxr_engine PUBLIC shim ${LXR_CORE}/Inc src PRIVATE ${LXR_GENERATED})
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

add_executable(timing_test tools/timing_test.c)
target_link_libraries(timing_test PRIVATE lxr_engine)

add_executable(list_params tools/list_params.c)
target_link_libraries(list_params PRIVATE lxr_engine)

if(LXR_HOST_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG 1.2.2
        FIND_PACKAGE_ARGS 1.2 CONFIG
    )
    FetchContent_MakeAvailable(clap)

    add_library(tack MODULE clap/tack.c clap/presets.c clap/engine.c)
    target_link_libraries(tack PRIVATE lxr_engine clap)
    # only clap_entry is exported; also keep statically linked dependencies' symbols out of the dynamic table
    target_link_options(tack PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)
    set_target_properties(tack PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Tack")

    # copies the plugin to ~/.clap in one step (copy, then rename), so a host that is watching the folder never
    # sees a half-written file, and rebuilding doesn't change what the host is running until this is run
    set(TACK_INSTALL_DIR "$ENV{HOME}/.clap" CACHE PATH "Where install_tack puts Tack.clap")
    add_custom_target(install_tack
        COMMAND ${CMAKE_COMMAND} -E make_directory ${TACK_INSTALL_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tack> ${TACK_INSTALL_DIR}/.Tack.clap.tmp
        COMMAND ${CMAKE_COMMAND} -E rename ${TACK_INSTALL_DIR}/.Tack.clap.tmp ${TACK_INSTALL_DIR}/Tack.clap
        DEPENDS tack
        COMMENT "Installing Tack.clap to ${TACK_INSTALL_DIR}")

    # test host; it links its own copy of the engine, which also checks that the plugin's copy stays separate
    add_executable(clap_render tools/clap_render.c)
    target_link_libraries(clap_render PRIVATE lxr_engine clap ${CMAKE_DL_LIBS})

    add_executable(multi_test tools/multi_test.c)
    target_link_libraries(multi_test PRIVATE clap ${CMAKE_DL_LIBS} pthread)
endif()
