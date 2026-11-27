cmake_minimum_required (VERSION 3.31)

set (target_name jamtaba)
set (target_version "0.1.0")

project (${target_name} VERSION ${target_version} LANGUAGES C CXX OBJC OBJCXX)

# Off for release packaging, so building never touches the installed plugins
option (JAMTABA_INSTALL_PLUGINS "Install the plugins into the user's plugin folders after each build" ON)
option (JAMTABA_BUILD_CLI "Build ninjam_cli, the headless engine harness" ON)

# ==== YUP. The submodule next to this file, unless YUP_DIR names another
# checkout, for trying a newer YUP before bumping the submodule:
#   cmake -S . -B build-yup -DYUP_DIR=$HOME/Documents/yup
# The release scripts always pass the submodule, so a release tag records the
# YUP it was built with.
set (YUP_DIR "${CMAKE_CURRENT_LIST_DIR}/yup" CACHE PATH "YUP source tree; the submodule by default")
file (REAL_PATH "${YUP_DIR}" yup_dir EXPAND_TILDE)
file (REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/yup" yup_submodule_dir)
if (NOT EXISTS "${yup_dir}/CMakeLists.txt")
    message (FATAL_ERROR "No YUP at ${yup_dir}. For the submodule, run: git submodule update --init --recursive")
endif ()
if (NOT yup_dir STREQUAL yup_submodule_dir)
    message (WARNING "Building against the YUP at ${yup_dir}, not the submodule")
endif ()
set (YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set (YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
set (YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory ("${yup_dir}" yup)

# ==== Identity. The AU codes are OSTypes: exactly four characters, and the
# manufacturer code needs at least one upper case letter.
set (plugin_name "JamTabaYUP")
set (plugin_description "Play online jam sessions on NINJAM servers.")
set (plugin_vendor "sousastep")
set (plugin_bundle_namespace "quest.sousastep")
set (plugin_email "")
set (plugin_url "https://github.com/jbaylies/JamTabaYUP")
set (plugin_au_manufacturer "Sous")
set (plugin_au_subtype "JmTb")
set (plugin_copyright "GPL-2.0-or-later. Inspired by JamTaba 2 (Elieser de Jesus); NINJAM protocol (c) Cockos Incorporated.")

# Audio in (the local channels) and the jam mix out
yup_audio_plugin (
    TARGET_NAME ${target_name}
    TARGET_VERSION ${target_version}
    TARGET_APP_ID "${plugin_bundle_namespace}.${plugin_name}"
    TARGET_APP_NAMESPACE "${plugin_bundle_namespace}"
    TARGET_CXX_STANDARD 20
    TARGET_IDE_GROUP "JamTabaYUP"
    PLUGIN_ID "${plugin_bundle_namespace}.${plugin_name}"
    PLUGIN_NAME "${plugin_name}"
    PLUGIN_VENDOR "${plugin_vendor}"
    PLUGIN_EMAIL "${plugin_email}"
    PLUGIN_VERSION "${target_version}"
    PLUGIN_DESCRIPTION "${plugin_description}"
    PLUGIN_URL "${plugin_url}"
    PLUGIN_IS_SYNTH OFF
    PLUGIN_IS_MONO OFF
    PLUGIN_CREATE_CLAP ON
    PLUGIN_CREATE_VST3 ON
    PLUGIN_CREATE_AU ON
    PLUGIN_CREATE_AUv3 OFF
    PLUGIN_CREATE_LV2 OFF
    PLUGIN_CREATE_AAX OFF
    PLUGIN_CREATE_STANDALONE ON
    PLUGIN_CLAP_FEATURES audio-effect utility
    PLUGIN_VST3_CATEGORIES Fx Network
    PLUGIN_AU_MANUFACTURER "${plugin_au_manufacturer}"
    PLUGIN_AU_SUBTYPE "${plugin_au_subtype}"
    PLUGIN_COPY_AFTER_BUILD ${JAMTABA_INSTALL_PLUGINS}
    DEFINITIONS
        YUP_EMBED_DEFAULT_THEME_TEXT_SERIF_FONT=1
        YUP_EMBED_DEFAULT_THEME_TEXT_MONOSPACE_FONT=1
    MODULES
        yup::yup_gui
        yup::yup_audio_processors
        libvorbis)

# ==== The standalone app records from the audio interface, and macOS hands an
# app silence unless its Info.plist says why it wants the microphone. YUP's
# template has no such key, so the app gets its own copy of it.
if (TARGET ${target_name}_standalone_plugin)
    set_target_properties (${target_name}_standalone_plugin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/cmake/StandaloneInfo.plist.in"
        MACOSX_BUNDLE_COPYRIGHT "${plugin_copyright}")
endif()

# ==== The standalone app's icon. yup_audio_plugin doesn't pass an icon on to
# its standalone target, so the app gets YUP's own icon; it is swapped for ours
# here: icons.icns and the Icon Composer catalog on macOS, the icon resource
# script on Windows.
if (TARGET ${target_name}_standalone_plugin)
    set (standalone ${target_name}_standalone_plugin)
    get_target_property (standalone_sources ${standalone} SOURCES)
    list (FILTER standalone_sources EXCLUDE REGEX "(icons\\.icns|Assets\\.car|_icon\\.rc)$")
    set_property (TARGET ${standalone} PROPERTY SOURCES ${standalone_sources})

    if (APPLE)
        set (app_icon "${CMAKE_CURRENT_LIST_DIR}/resources/JamTabaYUP.icns")
        get_target_property (standalone_resources ${standalone} RESOURCE)
        list (FILTER standalone_resources EXCLUDE REGEX "(icons\\.icns|Assets\\.car)$")
        list (APPEND standalone_resources "${app_icon}")
        target_sources (${standalone} PRIVATE "${app_icon}")
        set_target_properties (${standalone} PROPERTIES
            RESOURCE "${standalone_resources}"
            MACOSX_BUNDLE_ICON_FILE "JamTabaYUP.icns")
    elseif (WIN32)
        set (app_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/JamTabaYUP_icon.rc")
        file (WRITE "${app_icon_rc}" "1 ICON \"${CMAKE_CURRENT_LIST_DIR}/resources/JamTabaYUP.ico\"\n")
        target_sources (${standalone} PRIVATE "${app_icon_rc}")
    endif()
endif()

# ==== Install real copies of the VST3 and CLAP bundles. YUP symlinks them into
# ~/Library/Audio/Plug-Ins, and a link into a privacy-protected folder such as
# ~/Desktop is silently skipped by hosts without access to it (Ableton Live).
# The copy is signed again, ad hoc, since the bundle changes after linking.
if (NOT JAMTABA_INSTALL_PLUGINS)
    # nothing to install
elseif (APPLE)
    foreach (format vst3 clap)
        set (format_target ${target_name}_${format}_plugin)
        if (NOT TARGET ${format_target})
            continue()
        endif()

        string (TOUPPER "${format}" format_folder)
        set (install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/${format_folder}")
        add_custom_command (TARGET ${format_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${install_dir}/${format_target}.${format}" "${install_dir}/${plugin_name}.${format}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${format_target}>" "${install_dir}/${plugin_name}.${format}"
            COMMAND codesign --force --sign - "${install_dir}/${plugin_name}.${format}"
            COMMENT "Copying ${format_folder} plugin to ${install_dir}/${plugin_name}.${format}"
            VERBATIM)
    endforeach()
elseif (WIN32)
    # YUP installs nothing on Windows. The per-user folders from the VST3 and
    # CLAP specs need no administrator rights. A VST3 bundle's inner binary
    # must carry the bundle's name, so both are renamed.
    set (user_plugins "$ENV{LOCALAPPDATA}/Programs/Common")
    if (TARGET ${target_name}_vst3_plugin)
        set (vst3_bundle "${user_plugins}/VST3/${plugin_name}.vst3")
        add_custom_command (TARGET ${target_name}_vst3_plugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${vst3_bundle}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:${target_name}_vst3_plugin>/../.." "${vst3_bundle}"
            COMMAND ${CMAKE_COMMAND} -E rename "${vst3_bundle}/Contents/x86_64-win/$<TARGET_FILE_NAME:${target_name}_vst3_plugin>" "${vst3_bundle}/Contents/x86_64-win/${plugin_name}.vst3"
            COMMENT "Copying VST3 plugin to ${vst3_bundle}"
            VERBATIM)
    endif()
    if (TARGET ${target_name}_clap_plugin)
        add_custom_command (TARGET ${target_name}_clap_plugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${user_plugins}/CLAP"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target_name}_clap_plugin>" "${user_plugins}/CLAP/${plugin_name}.clap"
            COMMENT "Copying CLAP plugin to ${user_plugins}/CLAP/${plugin_name}.clap"
            VERBATIM)
    endif()
endif()

# ==== Strict warnings on our own files only. YUP and its externals are read as
# system headers here (quoted includes inside them inherit that).
if (MSVC)
    set (strict_warnings "/W4;/external:anglebrackets;/external:W0")
else()
    set (strict_warnings "--system-header-prefix=yup_;--system-header-prefix=xsimd/;--system-header-prefix=vorbis/;--system-header-prefix=ogg/;-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion;-Wfloat-conversion;-Wdouble-promotion")
endif()

# ==== Engine: no GUI, shared by the plugin and the console harness
file (GLOB_RECURSE engine_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/source/engine/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/engine/*.h")

# ==== Plugin and UI, compiled once into the core every format links
file (GLOB_RECURSE plugin_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/source/plugin/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/plugin/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/source/ui/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/ui/*.h")

set (all_sources ${engine_sources} ${plugin_sources})
source_group (TREE ${CMAKE_CURRENT_LIST_DIR}/source FILES ${all_sources})
target_sources (${target_name}_shared PRIVATE ${all_sources})
target_include_directories (${target_name}_shared PRIVATE "${CMAKE_CURRENT_LIST_DIR}/source")
set_source_files_properties (${all_sources} PROPERTIES COMPILE_OPTIONS "${strict_warnings}")

# ==== Headless harness: the same engine sources with no GUI modules
if (JAMTABA_BUILD_CLI)
    yup_standalone_app (
        TARGET_NAME ninjam_cli
        TARGET_VERSION ${target_version}
        TARGET_IDE_GROUP "JamTabaYUP"
        TARGET_APP_ID "${plugin_bundle_namespace}.ninjam_cli"
        TARGET_APP_NAMESPACE "${plugin_bundle_namespace}"
        TARGET_CONSOLE ON
        TARGET_CXX_STANDARD 20
        MODULES
            yup::yup_core
            yup::yup_events
            yup::yup_audio_basics
            libvorbis)

    file (GLOB_RECURSE cli_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/tools/ninjam_cli/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/tools/ninjam_cli/*.h")
    source_group (TREE ${CMAKE_CURRENT_LIST_DIR} FILES ${cli_sources})
    target_sources (ninjam_cli PRIVATE ${engine_sources} ${cli_sources})
    target_include_directories (ninjam_cli PRIVATE "${CMAKE_CURRENT_LIST_DIR}/source")
    set_source_files_properties (${cli_sources} PROPERTIES COMPILE_OPTIONS "${strict_warnings}")
endif()
