# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.21)

# Builds the LXR sound engine for a desktop computer, and the Tack plugin on it: gcc on Linux x86-64, mingw-w64
# for Windows (cmake/mingw-w64.cmake, also from Linux), Apple clang on macOS. This is a separate project from the
# firmware in the parent directory:
#
#   cmake -S host -B build/host
#   cmake --build build/host
#   build/host/lxr_render kit.wav

# VERSION is Tack's version: the plugins report it, and release tags (v0.1.0) must match it
project(LXR_Host VERSION 0.1.0 LANGUAGES C CXX)

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
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# 0.2.2 asks for a CMake version that CMake 4 no longer supports
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
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
    src/lxr_files.c
    src/lxr_params.c
    src/stubs.c
)
# shim/ stands in for the STM32 HAL and must come before the firmware headers
target_include_directories(lxr_engine PUBLIC shim ${LXR_CORE}/Inc src PRIVATE ${LXR_GENERATED})
# the firmware's headers leave out what only the device needs, such as its memory sections
target_compile_definitions(lxr_engine PUBLIC LXR_HOST_BUILD)
# -fcommon: the firmware defines some globals in headers
# -w: the firmware isn't warning-clean and the noise would bury host-side warnings
target_compile_options(lxr_engine PRIVATE -fcommon -w)
target_link_libraries(lxr_engine PUBLIC m PRIVATE SampleRate::samplerate)

if(MINGW)
    # a plugin and its tools that need no mingw runtime DLLs next to them
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

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

add_executable(make_kit tools/make_kit.c)
target_link_libraries(make_kit PRIVATE lxr_engine)

if(LXR_HOST_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG 1.2.2
        FIND_PACKAGE_ARGS 1.2 CONFIG
    )
    FetchContent_MakeAvailable(clap)

    # The editor: Dear ImGui drawing into a pugl OpenGL view. Neither has a CMake build, so compile their sources.
    # pugl (ISC) is pinned to a commit since it has no recent release tags; Dear ImGui (MIT) to a release.
    FetchContent_Declare(pugl
        GIT_REPOSITORY https://github.com/lv2/pugl.git
        GIT_TAG b7637149ebe53124e5be90559e02a0185bbcbd73
        SOURCE_SUBDIR no-cmake
    )
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        SOURCE_SUBDIR no-cmake
    )
    FetchContent_MakeAvailable(pugl imgui)
    find_package(OpenGL REQUIRED)

    # pugl's backend for the platform: the window system, its OpenGL and a plain window (for gui_test's host window)
    set(PUGL_SRC ${pugl_SOURCE_DIR}/src)
    if(WIN32)
        set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/win.c ${PUGL_SRC}/win_gl.c ${PUGL_SRC}/win_stub.c)
        set(PUGL_PLATFORM_LIBS dwmapi gdi32 shell32 shlwapi user32 OpenGL::GL)
    elseif(APPLE)
        enable_language(OBJC)
        set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/mac.m ${PUGL_SRC}/mac_gl.m ${PUGL_SRC}/mac_stub.m)
        set(PUGL_PLATFORM_LIBS "-framework Cocoa" "-framework CoreVideo" OpenGL::GL)
    else()
        find_package(X11 REQUIRED)
        set(PUGL_PLATFORM_SOURCES ${PUGL_SRC}/x11.c ${PUGL_SRC}/x11_gl.c ${PUGL_SRC}/x11_stub.c)
        set(PUGL_PLATFORM_LIBS X11::X11 OpenGL::GL)
    endif()

    add_library(tack_gui_deps STATIC
        ${PUGL_SRC}/common.c
        ${PUGL_SRC}/internal.c
        ${PUGL_PLATFORM_SOURCES}
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(tack_gui_deps PUBLIC ${pugl_SOURCE_DIR}/include ${imgui_SOURCE_DIR})
    # PUGL_API is visibility("default") unless defined: keep pugl out of Tack's exports, so it can't clash with
    # another plugin's pugl in the same host. Only libX11 and libGL are needed on Linux.
    target_compile_definitions(tack_gui_deps PUBLIC PUGL_STATIC PUGL_API= PUGL_DISABLE_DEPRECATED
        PRIVATE USE_XRANDR=0 USE_XSYNC=0 USE_XCURSOR=0 IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    if(WIN32)
        target_compile_definitions(tack_gui_deps PRIVATE UNICODE _UNICODE)
    elseif(APPLE)
        # Objective-C classes are global to the process: name pugl's after Tack, so that another plugin's pugl
        # can't clash with them
        target_compile_definitions(tack_gui_deps PUBLIC GL_SILENCE_DEPRECATION
            PuglWrapperView=TackPuglWrapperView PuglWindow=TackPuglWindow
            PuglWindowDelegate=TackPuglWindowDelegate PuglOpenGLView=TackPuglOpenGLView
            PuglStubView=TackPuglStubView)
    endif()
    target_compile_options(tack_gui_deps PRIVATE -w)
    target_link_libraries(tack_gui_deps PUBLIC ${PUGL_PLATFORM_LIBS} ${CMAKE_DL_LIBS})

    add_library(tack MODULE clap/tack.c clap/presets.c clap/engine.c clap/gui.cpp)
    target_link_libraries(tack PRIVATE lxr_engine clap tack_gui_deps)
    target_compile_definitions(tack PRIVATE TACK_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        # the kit folder in Documents
        target_link_libraries(tack PRIVATE shell32 ole32 uuid)
        set_target_properties(tack PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Tack")
    elseif(APPLE)
        # a bundle, Tack.clap/Contents/MacOS/Tack, with the engine library the instances copy in
        # Contents/Frameworks (engine.c)
        set_target_properties(tack PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION clap OUTPUT_NAME "Tack"
            MACOSX_BUNDLE_GUI_IDENTIFIER com.oikoaudio.tack MACOSX_BUNDLE_BUNDLE_NAME Tack
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION} MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in)
        target_link_options(tack PRIVATE -Wl,-exported_symbol,_clap_entry)

        add_library(tack_engine MODULE clap/engine.c)
        target_compile_definitions(tack_engine PRIVATE TACK_ENGINE_ONLY)
        target_link_libraries(tack_engine PRIVATE lxr_engine)
        target_link_options(tack_engine PRIVATE -Wl,-exported_symbol,_tack_engine)
        set_target_properties(tack_engine PROPERTIES PREFIX "" SUFFIX ".dylib" OUTPUT_NAME "TackEngine")
        add_custom_command(TARGET tack POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:tack>/Frameworks
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tack_engine> $<TARGET_BUNDLE_CONTENT_DIR:tack>/Frameworks/
            # ad-hoc signed, so that Apple silicon runs it; a release would be signed with an identity
            COMMAND codesign --force --sign - $<TARGET_BUNDLE_CONTENT_DIR:tack>/Frameworks/TackEngine.dylib
            COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:tack>)
        add_dependencies(tack tack_engine)
    else()
        # only clap_entry is exported; also keep statically linked dependencies' symbols out of the dynamic table. The
        # C++ runtime is linked in, so the plugin doesn't depend on the host's libstdc++ version
        target_link_options(tack PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined -static-libstdc++ -static-libgcc)
        set_target_properties(tack PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Tack")
    endif()

    # copies the plugin to the user's CLAP folder in one step (copy, then rename), so a host that is watching the
    # folder never sees a half-written file, and rebuilding doesn't change what the host is running until this is run
    if(WIN32)
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
    elseif(APPLE)
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    else()
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{HOME}/.clap")
    endif()
    set(TACK_INSTALL_DIR "${TACK_DEFAULT_INSTALL_DIR}" CACHE PATH "Where install_tack puts Tack.clap")
    if(APPLE)
        add_custom_target(install_tack
            COMMAND ${CMAKE_COMMAND} -E make_directory ${TACK_INSTALL_DIR}
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${TACK_INSTALL_DIR}/.Tack.clap.tmp
            COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_BUNDLE_DIR:tack> ${TACK_INSTALL_DIR}/.Tack.clap.tmp
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${TACK_INSTALL_DIR}/Tack.clap
            COMMAND ${CMAKE_COMMAND} -E rename ${TACK_INSTALL_DIR}/.Tack.clap.tmp ${TACK_INSTALL_DIR}/Tack.clap
            DEPENDS tack
            COMMENT "Installing Tack.clap to ${TACK_INSTALL_DIR}")
    else()
        add_custom_target(install_tack
            COMMAND ${CMAKE_COMMAND} -E make_directory ${TACK_INSTALL_DIR}
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tack> ${TACK_INSTALL_DIR}/.Tack.clap.tmp
            COMMAND ${CMAKE_COMMAND} -E rename ${TACK_INSTALL_DIR}/.Tack.clap.tmp ${TACK_INSTALL_DIR}/Tack.clap
            DEPENDS tack
            COMMENT "Installing Tack.clap to ${TACK_INSTALL_DIR}")
    endif()

    # A zip with the plugin, the README and the licences that come with it, for releases:
    # package/Tack-<version>-<platform>.zip in the build folder. The licences of the libraries linked in come from
    # their fetched sources, so this needs FETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER (which also links libsamplerate
    # statically).
    set(TACK_PACKAGE_PLATFORM "" CACHE STRING "Platform in the release zip's name, e.g. linux-x86_64; empty: no package_tack target")
    if(TACK_PACKAGE_PLATFORM)
        if(NOT clap_SOURCE_DIR OR NOT samplerate_SOURCE_DIR)
            message(FATAL_ERROR "package_tack needs fetched dependencies: configure with -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER")
        endif()
        set(PKG_NAME Tack-${PROJECT_VERSION}-${TACK_PACKAGE_PLATFORM})
        set(PKG_DIR ${CMAKE_BINARY_DIR}/package/${PKG_NAME})
        if(APPLE)
            set(PKG_COPY_PLUGIN ${CMAKE_COMMAND} -E copy_directory $<TARGET_BUNDLE_DIR:tack> ${PKG_DIR}/Tack.clap)
        else()
            set(PKG_COPY_PLUGIN ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tack> ${PKG_DIR}/Tack.clap)
        endif()
        set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
        add_custom_target(package_tack
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${PKG_DIR} ${PKG_DIR}.zip
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PKG_DIR}/licenses
            COMMAND ${PKG_COPY_PLUGIN}
            COMMAND ${CMAKE_COMMAND} -E copy ${ROOT}/README.md ${ROOT}/LICENSE ${ROOT}/NOTICE.md ${PKG_DIR}
            # the original LXR firmware's licence: its notice, conditions and disclaimer come with every copy
            COMMAND ${CMAKE_COMMAND} -E copy ${ROOT}/LICENSES/SonicPotions-LXR.txt ${PKG_DIR}/licenses/
            COMMAND ${CMAKE_COMMAND} -E copy ${clap_SOURCE_DIR}/LICENSE ${PKG_DIR}/licenses/clap-LICENSE.txt
            COMMAND ${CMAKE_COMMAND} -E copy ${pugl_SOURCE_DIR}/COPYING ${PKG_DIR}/licenses/pugl-COPYING.txt
            COMMAND ${CMAKE_COMMAND} -E copy ${imgui_SOURCE_DIR}/LICENSE.txt ${PKG_DIR}/licenses/imgui-LICENSE.txt
            COMMAND ${CMAKE_COMMAND} -E copy ${samplerate_SOURCE_DIR}/COPYING ${PKG_DIR}/licenses/libsamplerate-COPYING.txt
            COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/package ${CMAKE_COMMAND} -E tar cf ${PKG_NAME}.zip --format=zip ${PKG_NAME}
            DEPENDS tack
            COMMENT "Packaging ${PKG_NAME}.zip")
    endif()

    # test host; it links its own copy of the engine, which also checks that the plugin's copy stays separate
    add_executable(clap_render tools/clap_render.c)
    target_link_libraries(clap_render PRIVATE lxr_engine clap ${CMAKE_DL_LIBS})

    # its window is a plain pugl view, on every platform
    add_executable(gui_test tools/gui_test.c)
    target_link_libraries(gui_test PRIVATE clap tack_gui_deps ${CMAKE_DL_LIBS})

    find_package(Threads REQUIRED)
    add_executable(multi_test tools/multi_test.c)
    target_link_libraries(multi_test PRIVATE clap ${CMAKE_DL_LIBS} Threads::Threads)
endif()
