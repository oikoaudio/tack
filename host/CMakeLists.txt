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
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/lxr_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/kit_name.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/kit_mutate.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/lfo_speed.c
    src/lxr_host.c
    src/lxr_files.c
    src/lxr_params.c
    src/stubs.c
)
# shim/ stands in for the STM32 HAL and must come before the firmware headers
target_include_directories(lxr_engine PUBLIC shim ${LXR_CORE}/Inc src ../engine PRIVATE ${LXR_GENERATED})
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

add_executable(mutate_test tools/mutate_test.c)
target_link_libraries(mutate_test PRIVATE lxr_engine)

add_executable(lfo_test tools/lfo_test.c)
target_link_libraries(lfo_test PRIVATE lxr_engine)

add_executable(make_kit tools/make_kit.c)
target_link_libraries(make_kit PRIVATE lxr_engine)

if(LXR_HOST_CLAP)
    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG 1.2.10
        # clap-wrapper uses the newest extensions
        FIND_PACKAGE_ARGS 1.2.10 CONFIG
    )
    FetchContent_MakeAvailable(clap)

    # The editor: Dear ImGui drawing into a pugl OpenGL view. Neither has a CMake build, so compile their sources.
    # pugl (ISC) is pinned to a commit since it has no recent release tags; Dear ImGui (MIT) to a release.
    # pugl gets a patch so that keys the editor doesn't use reach the host, such as space for play and stop.
    FetchContent_Declare(pugl
        GIT_REPOSITORY https://github.com/lv2/pugl.git
        GIT_TAG b7637149ebe53124e5be90559e02a0185bbcbd73
        PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/pugl-keys.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply_patch.cmake
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
        # Objective-C++: clap-wrapper's macOS sources, which compile in Tack's targets
        enable_language(OBJC OBJCXX)
        # built once for each binary, see tack_pugl_mac()
        set(PUGL_PLATFORM_SOURCES "")
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
        target_compile_definitions(tack_gui_deps PUBLIC GL_SILENCE_DEPRECATION)
    endif()
    target_compile_options(tack_gui_deps PRIVATE -w)
    target_link_libraries(tack_gui_deps PUBLIC ${PUGL_PLATFORM_LIBS} ${CMAKE_DL_LIBS})

    # macOS: links pugl's Cocoa backend into <target>, its Objective-C classes named <prefix>PuglWrapperView and so
    # on. Those classes are global to the process, so each binary gets its own names: another plugin's pugl, or the
    # CLAP, VST3 and AU of Tack loaded by the same host, can't clash.
    function(tack_pugl_mac target prefix)
        if(NOT APPLE)
            return()
        endif()
        add_library(${target}_pugl STATIC ${PUGL_SRC}/mac.m ${PUGL_SRC}/mac_gl.m ${PUGL_SRC}/mac_stub.m)
        target_link_libraries(${target}_pugl PUBLIC tack_gui_deps)
        target_compile_definitions(${target}_pugl PRIVATE PuglWrapperView=${prefix}PuglWrapperView
            PuglWindow=${prefix}PuglWindow PuglWindowDelegate=${prefix}PuglWindowDelegate
            PuglOpenGLView=${prefix}PuglOpenGLView PuglStubView=${prefix}PuglStubView)
        target_compile_options(${target}_pugl PRIVATE -w)
        target_link_libraries(${target} PRIVATE ${target}_pugl)
    endfunction()

    # clap-wrapper (MIT) makes the VST3 and the Audio Unit from the CLAP. Its extension headers are part of the
    # plugin's sources (clap/wrapper_info.cpp), so it is always fetched; the format SDKs only for the formats built.
    FetchContent_Declare(clap-wrapper
        GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
        GIT_TAG v0.17.0
    )
    FetchContent_MakeAvailable(clap-wrapper)

    # Tack's own code, linked into every format
    add_library(tack_plugin OBJECT clap/tack.c clap/presets.c clap/engine.c clap/gui.cpp clap/wrapper_info.cpp)
    target_link_libraries(tack_plugin PUBLIC lxr_engine clap tack_gui_deps clap-wrapper-extensions)
    target_compile_definitions(tack_plugin PRIVATE TACK_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        # the kit folder in Documents
        target_link_libraries(tack_plugin PUBLIC shell32 ole32 uuid)
    endif()

    add_library(tack MODULE)
    target_link_libraries(tack PRIVATE tack_plugin)
    tack_pugl_mac(tack TackClap)
    if(WIN32)
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

        # every bundle (CLAP, VST3, AU) carries the engine library, ad-hoc signed so that Apple silicon runs it; a
        # release would be signed with an identity
        function(tack_bundle_engine target)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:tack_engine> $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks/
                COMMAND codesign --force --sign - $<TARGET_BUNDLE_CONTENT_DIR:${target}>/Frameworks/TackEngine.dylib
                COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:${target}>)
            add_dependencies(${target} tack_engine)
        endfunction()
        tack_bundle_engine(tack)
    else()
        # only clap_entry is exported; also keep statically linked dependencies' symbols out of the dynamic table. The
        # C++ runtime is linked in, so the plugin doesn't depend on the host's libstdc++ version
        target_link_options(tack PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined -static-libstdc++ -static-libgcc)
        set_target_properties(tack PROPERTIES PREFIX "" SUFFIX ".clap" OUTPUT_NAME "Tack")
    endif()
    # the CLAP's file or bundle
    if(APPLE)
        set(TACK_CLAP_PATH $<TARGET_BUNDLE_DIR:tack>)
    else()
        set(TACK_CLAP_PATH $<TARGET_FILE:tack>)
    endif()
    set(TACK_FORMATS tack)

    # The VST3 (and on macOS the Audio Unit): the same code with clap-wrapper's VST3 or AU side linked in, so each is
    # complete on its own and doesn't load Tack.clap
    option(TACK_VST3_AU "Build Tack.vst3, and Tack.component on macOS, with clap-wrapper" ON)
    if(TACK_VST3_AU)
        # MIT since 3.8; only the parts a plugin needs
        FetchContent_Declare(vst3sdk
            GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
            GIT_TAG v3.8.0_build_66
            GIT_SUBMODULES base public.sdk pluginterfaces cmake
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR no-cmake
        )
        FetchContent_MakeAvailable(vst3sdk)
        set(VST3_SDK_ROOT ${vst3sdk_SOURCE_DIR})

        add_library(tack_vst3 MODULE)
        target_link_libraries(tack_vst3 PRIVATE tack_plugin)
        tack_pugl_mac(tack_vst3 TackVst3)
        # a bundle (a folder) on every platform
        target_add_vst3_wrapper(TARGET tack_vst3 OUTPUT_NAME Tack
            BUNDLE_IDENTIFIER com.oikoaudio.tack.vst3 BUNDLE_VERSION ${PROJECT_VERSION}
            ASSET_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} WINDOWS_FOLDER_VST3 TRUE)
        set_target_properties(tack_vst3 PROPERTIES PREFIX "")
        if(APPLE)
            tack_bundle_engine(tack_vst3)
            set(TACK_VST3_PATH $<TARGET_BUNDLE_DIR:tack_vst3>)
        else()
            set(TACK_VST3_PATH ${CMAKE_CURRENT_BINARY_DIR}/Tack.vst3)
        endif()
        if(NOT WIN32 AND NOT APPLE)
            # the VST3 entry points and the engine for the instances' copies (engine.c), nothing else
            target_link_options(tack_vst3 PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/clap/vst3.map
                -static-libstdc++ -static-libgcc)
            set_property(TARGET tack_vst3 APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/clap/vst3.map)
        endif()
        if(NOT APPLE)
            # The wrapper's own way of finding clap_entry in its binary doesn't work on Linux (and would fall back to
            # loading an installed Tack.clap): link it to Tack's directly. Not on macOS, where the AU's build
            # helper links the same library without Tack.
            target_compile_definitions(clap-wrapper-shared-detail PRIVATE STATICALLY_LINKED_CLAP_ENTRY=1)
        endif()
        list(APPEND TACK_FORMATS tack_vst3)

        # test host, on the VST3 SDK's hosting classes
        set(V ${VST3_SDK_ROOT})
        file(GLOB VST3_HOST_SOURCES ${V}/base/source/*.cpp ${V}/base/thread/source/*.cpp ${V}/pluginterfaces/base/*.cpp
            ${V}/public.sdk/source/common/*.cpp)
        if(WIN32)
            set(VST3_MODULE_SOURCE ${V}/public.sdk/source/vst/hosting/module_win32.cpp)
        elseif(APPLE)
            set(VST3_MODULE_SOURCE ${V}/public.sdk/source/vst/hosting/module_mac.mm)
            set_source_files_properties(${VST3_MODULE_SOURCE} PROPERTIES COMPILE_OPTIONS "-w;-fobjc-arc")
        else()
            set(VST3_MODULE_SOURCE ${V}/public.sdk/source/vst/hosting/module_linux.cpp)
        endif()
        add_executable(vst3_test tools/vst3_test.cpp ${VST3_HOST_SOURCES} ${VST3_MODULE_SOURCE}
            ${V}/public.sdk/source/vst/hosting/module.cpp
            ${V}/public.sdk/source/vst/hosting/hostclasses.cpp
            ${V}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
            ${V}/public.sdk/source/vst/hosting/processdata.cpp
            ${V}/public.sdk/source/vst/hosting/eventlist.cpp
            ${V}/public.sdk/source/vst/hosting/parameterchanges.cpp
            ${V}/public.sdk/source/vst/utility/stringconvert.cpp
            ${V}/public.sdk/source/vst/vstinitiids.cpp)
        target_include_directories(vst3_test PRIVATE ${V})
        target_compile_definitions(vst3_test PRIVATE $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
        # the SDK's own warnings
        set_source_files_properties(${VST3_HOST_SOURCES} PROPERTIES COMPILE_OPTIONS -w)
        if(NOT APPLE)
            set_source_files_properties(${VST3_MODULE_SOURCE} PROPERTIES COMPILE_OPTIONS -w)
        endif()
        target_link_libraries(vst3_test PRIVATE ${CMAKE_DL_LIBS})
        if(WIN32)
            target_link_libraries(vst3_test PRIVATE ole32 shell32)
        elseif(APPLE)
            target_link_libraries(vst3_test PRIVATE "-framework Cocoa")
        else()
            find_package(Threads REQUIRED)
            target_link_libraries(vst3_test PRIVATE Threads::Threads)
        endif()

        # Apache 2.0. Fetched on every platform, so that the release's source archive (made on Linux) has it.
        FetchContent_Declare(AudioUnitSDK
            GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
            GIT_TAG AudioUnitSDK-1.1.0
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR no-cmake
        )
        FetchContent_MakeAvailable(AudioUnitSDK)
        if(APPLE)
            set(AUDIOUNIT_SDK_ROOT ${audiounitsdk_SOURCE_DIR})

            # the build helper reads the components (one per plugin, codes in clap/wrapper_info.cpp) from the CLAP
            set_target_properties(tack PROPERTIES LIBRARY_OUTPUT_NAME Tack)
            add_library(tack_auv2 MODULE)
            target_link_libraries(tack_auv2 PRIVATE tack_plugin)
            tack_pugl_mac(tack_auv2 TackAu)
            target_add_auv2_wrapper(TARGET tack_auv2 OUTPUT_NAME Tack
                BUNDLE_IDENTIFIER com.oikoaudio.tack BUNDLE_VERSION ${PROJECT_VERSION}
                CLAP_TARGET_FOR_CONFIG tack)
            tack_bundle_engine(tack_auv2)
            set(TACK_AU_PATH $<TARGET_BUNDLE_DIR:tack_auv2>)
            list(APPEND TACK_FORMATS tack_auv2)
        endif()
    endif()

    # install_tack copies each format to the user's plugin folder in one step (copy, then rename), so a host that is
    # watching the folder never sees a half-written file, and rebuilding doesn't change what the host is running until
    # this is run
    if(WIN32)
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{LOCALAPPDATA}/Programs/Common/CLAP")
        set(TACK_DEFAULT_VST3_INSTALL_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
    elseif(APPLE)
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
        set(TACK_DEFAULT_VST3_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    else()
        set(TACK_DEFAULT_INSTALL_DIR "$ENV{HOME}/.clap")
        set(TACK_DEFAULT_VST3_INSTALL_DIR "$ENV{HOME}/.vst3")
    endif()
    set(TACK_INSTALL_DIR "${TACK_DEFAULT_INSTALL_DIR}" CACHE PATH "Where install_tack puts Tack.clap")
    set(TACK_VST3_INSTALL_DIR "${TACK_DEFAULT_VST3_INSTALL_DIR}" CACHE PATH "Where install_tack puts Tack.vst3")
    set(TACK_AU_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components" CACHE PATH
        "Where install_tack puts Tack.component (macOS)")

    # appends to <out> the commands that copy a file or a bundle (a folder) to <dest>
    function(tack_copy_commands out src dest bundle)
        get_filename_component(dir ${dest} DIRECTORY)
        get_filename_component(name ${dest} NAME)
        set(c ${${out}} COMMAND ${CMAKE_COMMAND} -E make_directory ${dir})
        if(bundle)
            list(APPEND c COMMAND ${CMAKE_COMMAND} -E rm -rf ${dir}/.${name}.tmp
                COMMAND ${CMAKE_COMMAND} -E copy_directory ${src} ${dir}/.${name}.tmp
                COMMAND ${CMAKE_COMMAND} -E rm -rf ${dest})
        else()
            list(APPEND c COMMAND ${CMAKE_COMMAND} -E copy ${src} ${dir}/.${name}.tmp)
        endif()
        list(APPEND c COMMAND ${CMAKE_COMMAND} -E rename ${dir}/.${name}.tmp ${dest})
        set(${out} ${c} PARENT_SCOPE)
    endfunction()

    # the bundles (folders): the CLAP on macOS, the VST3 everywhere
    set(TACK_CLAP_BUNDLE FALSE)
    if(APPLE)
        set(TACK_CLAP_BUNDLE TRUE)
    endif()

    set(INSTALL_COMMANDS "")
    tack_copy_commands(INSTALL_COMMANDS ${TACK_CLAP_PATH} ${TACK_INSTALL_DIR}/Tack.clap ${TACK_CLAP_BUNDLE})
    set(INSTALLED "Tack.clap to ${TACK_INSTALL_DIR}")
    if(TACK_VST3_PATH)
        tack_copy_commands(INSTALL_COMMANDS ${TACK_VST3_PATH} ${TACK_VST3_INSTALL_DIR}/Tack.vst3 TRUE)
        string(APPEND INSTALLED ", Tack.vst3 to ${TACK_VST3_INSTALL_DIR}")
    endif()
    if(TACK_AU_PATH)
        tack_copy_commands(INSTALL_COMMANDS ${TACK_AU_PATH} ${TACK_AU_INSTALL_DIR}/Tack.component TRUE)
        string(APPEND INSTALLED ", Tack.component to ${TACK_AU_INSTALL_DIR}")
    endif()
    add_custom_target(install_tack ${INSTALL_COMMANDS} DEPENDS ${TACK_FORMATS} COMMENT "Installing ${INSTALLED}")

    # A zip with the plugins, the README and the licences that come with them, for releases:
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
        set(PKG_COMMANDS "")
        tack_copy_commands(PKG_COMMANDS ${TACK_CLAP_PATH} ${PKG_DIR}/Tack.clap ${TACK_CLAP_BUNDLE})
        set(LICENSE_FILES
            ${clap_SOURCE_DIR}/LICENSE clap-LICENSE.txt
            ${pugl_SOURCE_DIR}/COPYING pugl-COPYING.txt
            ${imgui_SOURCE_DIR}/LICENSE.txt imgui-LICENSE.txt
            ${samplerate_SOURCE_DIR}/COPYING libsamplerate-COPYING.txt)
        if(TACK_VST3_PATH)
            tack_copy_commands(PKG_COMMANDS ${TACK_VST3_PATH} ${PKG_DIR}/Tack.vst3 TRUE)
            list(APPEND LICENSE_FILES
                ${clap-wrapper_SOURCE_DIR}/LICENSE clap-wrapper-LICENSE.txt
                ${vst3sdk_SOURCE_DIR}/LICENSE.txt vst3sdk-LICENSE.txt)
        endif()
        if(TACK_AU_PATH)
            tack_copy_commands(PKG_COMMANDS ${TACK_AU_PATH} ${PKG_DIR}/Tack.component TRUE)
            list(APPEND LICENSE_FILES ${audiounitsdk_SOURCE_DIR}/LICENSE.txt AudioUnitSDK-LICENSE.txt)
        endif()
        while(LICENSE_FILES)
            list(POP_FRONT LICENSE_FILES from to)
            list(APPEND PKG_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy ${from} ${PKG_DIR}/licenses/${to})
        endwhile()
        set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
        add_custom_target(package_tack
            COMMAND ${CMAKE_COMMAND} -E rm -rf ${PKG_DIR} ${PKG_DIR}.zip
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PKG_DIR}/licenses
            ${PKG_COMMANDS}
            COMMAND ${CMAKE_COMMAND} -E copy ${ROOT}/README.md ${ROOT}/LICENSE ${ROOT}/NOTICE.md ${PKG_DIR}
            # the original LXR firmware's licence: its notice, conditions and disclaimer come with every copy
            COMMAND ${CMAKE_COMMAND} -E copy ${ROOT}/LICENSES/SonicPotions-LXR.txt ${PKG_DIR}/licenses/
            COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/package ${CMAKE_COMMAND} -E tar cf ${PKG_NAME}.zip --format=zip ${PKG_NAME}
            DEPENDS ${TACK_FORMATS}
            COMMENT "Packaging ${PKG_NAME}.zip")
    endif()

    # test host; it links its own copy of the engine, which also checks that the plugin's copy stays separate
    add_executable(clap_render tools/clap_render.c)
    target_link_libraries(clap_render PRIVATE lxr_engine clap ${CMAKE_DL_LIBS})

    # its window is a plain pugl view, on every platform
    add_executable(gui_test tools/gui_test.c)
    target_link_libraries(gui_test PRIVATE clap tack_gui_deps ${CMAKE_DL_LIBS})
    tack_pugl_mac(gui_test TackGuiTest)

    find_package(Threads REQUIRED)
    add_executable(multi_test tools/multi_test.c)
    target_link_libraries(multi_test PRIVATE clap ${CMAKE_DL_LIBS} Threads::Threads)
endif()
