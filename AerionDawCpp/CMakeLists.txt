cmake_minimum_required(VERSION 3.20)
project(AerionDaw VERSION 0.2.0)

# Fetch Tracktion Engine (includes JUCE)
include(FetchContent)
set(TRACKTION_BUILD_EXAMPLES OFF CACHE BOOL "Disable Tracktion examples" FORCE)
set(TRACKTION_BUILD_TESTS OFF CACHE BOOL "Disable Tracktion tests" FORCE)
set(TRACKTION_BUILD_BENCHMARKS OFF CACHE BOOL "Disable Tracktion benchmarks" FORCE)
set(TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH ON CACHE BOOL "Enable SoundTouch" FORCE)

FetchContent_Declare(
    tracktion_engine
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
    GIT_TAG        v3.2.0
    GIT_SHALLOW    TRUE
)

# Fetch ONNX Runtime (C API)
FetchContent_Declare(
    onnxruntime
    GIT_REPOSITORY https://github.com/microsoft/onnxruntime.git
    GIT_TAG        v1.19.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(tracktion_engine)
# Note: We won't FetchContent_MakeAvailable(onnxruntime) because it's massive to build from source.
# In a real environment, we'd use prebuilt binaries. For this prototype, 
# we'll stub the AI logic if the build takes too long, but we'll try to set up the structure.

# Setup executable
juce_add_gui_app(AerionDaw
    PRODUCT_NAME "Aerion DAW"
    VERSION 0.2.0
    COMPANY_NAME "Aethos Studio Ltd."
    BUNDLE_ID "com.aethos.aeriondaw"
    ICON_BIG "Resources/aerion_logo.svg"
    ICON_SMALL "Resources/aerion_logo.svg"
)

juce_add_binary_data(AerionDawResources
    SOURCES
        Resources/aerion_logo.svg
        Resources/aerion_logo_vertical.svg
        Resources/aerion_fader.svg
        Resources/asio_compatible_logo.svg
        Resources/Cinzel-Regular.ttf
        Resources/Cinzel-Bold.ttf
        Resources/Icons/aerion_inspector.svg
        Resources/Icons/aerion_browser.svg
        Resources/Icons/aerion_select.svg
        Resources/Icons/aerion_cut.svg
        Resources/Icons/aerion_comp.svg
        Resources/Icons/aerion_punch.svg
        Resources/Icons/aerion_pdc.svg
        Resources/Icons/aerion_magnet.svg
        Resources/Icons/aerion_metronome.svg
        Resources/Icons/aerion_countin.svg
        Resources/Icons/aerion_arm.svg
        Resources/Icons/aerion_mute.svg
        Resources/Icons/aerion_Solo.svg
)

juce_generate_juce_header(AerionDaw)

# Add source files
target_sources(AerionDaw PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/UI/ThemeTypefaces.cpp
    Source/AerionTooltipWindow.h
    Source/AerionTooltipWindow.cpp
    Source/ProjectData.h
    Source/ProjectData.cpp
    Source/AudioEngine.h
    Source/AudioEngine.cpp
    Source/LogoComponent.h
    Source/GoogleDriveClient.h
    Source/GoogleDriveClient.cpp
    Source/AIManager.h
    Source/AIManager.cpp
    Source/Export/MixdownExportDialog.h
    Source/Export/MixdownExportDialog.cpp
    Source/Export/MixdownExportJob.h
    Source/Export/MixdownExportJob.cpp
    Source/Export/WaveformPreviewComponent.h
)

# Link necessary JUCE and Tracktion modules
target_link_libraries(AerionDaw PRIVATE
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_cryptography
    tracktion_core
    tracktion_graph
    tracktion_engine
    AerionDawResources
)

# Compile modern C++
target_compile_features(AerionDaw PRIVATE cxx_std_20)

# Enable plugin hosting in juce_audio_processors. Without these, the
# format manager has zero formats and PluginDirectoryScanner is a no-op.
target_compile_definitions(AerionDaw PRIVATE
    JUCE_PLUGINHOST_VST3=1
    $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>
)

# --- Audio backends ------------------------------------------------------------
# JUCE's juce_audio_devices already enables the platform-native backends by
# default (WASAPI + DirectSound on Windows, CoreAudio on macOS, ALSA on Linux).
# This block adds the other "industry standard" backends that are opt-in due
# to external SDK / library dependencies, auto-detecting them so each user's
# build picks up whatever is actually installed on their machine.

# ---- ASIO (Windows-only, GPLv3, on by default) --------------------------------
# Steinberg's ASIO SDK is now available under GPLv3 (https://www.steinberg.net/
# developers/), which is GPL-compatible with the rest of the Aerion stack
# (Tracktion Engine v3.2 is GPL). We therefore include the SDK headers in the
# repo (under AerionDawCpp/External/) and enable JUCE_ASIO for **all build
# configurations** when the SDK is present, so a fresh clone gets ASIO in both
# Debug and Release without any extra steps.
#
# Override with -DAERION_ENABLE_ASIO=<AUTO|NEVER|DEBUG_ONLY>:
#   AUTO         (default) - all configs, when the SDK is present
#   NEVER                  - never compile ASIO (use to validate a SDK-free build)
#   DEBUG_ONLY             - legacy mode: only enable ASIO in Debug configs
#
# The SDK is auto-discovered under AerionDawCpp/External/ (any folder name),
# or via -DAERION_ASIO_SDK_DIR=<path> / the ASIO_SDK_DIR environment variable.
#
# Trademark notice: "ASIO" is a trademark of Steinberg Media Technologies GmbH.
# Aerion uses the official ASIO-compatible logo and attribution per Steinberg's
# trademark guidelines (see Resources/asio_compatible_logo.svg + About box +
# Splash screen).
set(AERION_ENABLE_ASIO "AUTO" CACHE STRING
    "ASIO inclusion policy: AUTO (all configs), NEVER, DEBUG_ONLY.")
set_property(CACHE AERION_ENABLE_ASIO PROPERTY STRINGS AUTO NEVER DEBUG_ONLY)

if (WIN32 AND NOT AERION_ENABLE_ASIO STREQUAL "NEVER")
    if (NOT DEFINED AERION_ASIO_SDK_DIR AND DEFINED ENV{ASIO_SDK_DIR})
        set(AERION_ASIO_SDK_DIR "$ENV{ASIO_SDK_DIR}")
    endif()

    # Probe explicit overrides first, then walk a small tree under External/ so
    # the SDK works whether the user renamed it to /asiosdk or kept the
    # versioned name (e.g. ASIO-SDK_2.3.4_2025-10-15/ASIOSDK).
    set(_aerion_asio_explicit_dirs
        "${AERION_ASIO_SDK_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/External/asiosdk"
        "${CMAKE_CURRENT_SOURCE_DIR}/../External/asiosdk")

    set(_aerion_asio_search_roots
        "${CMAKE_CURRENT_SOURCE_DIR}/External"
        "${CMAKE_CURRENT_SOURCE_DIR}/../External")

    set(_aerion_asio_common "")

    foreach (_cand IN LISTS _aerion_asio_explicit_dirs)
        if (_cand AND EXISTS "${_cand}/common/iasiodrv.h")
            set(_aerion_asio_common "${_cand}/common")
            break()
        endif()
    endforeach()

    if (NOT _aerion_asio_common)
        foreach (_root IN LISTS _aerion_asio_search_roots)
            if (EXISTS "${_root}")
                file(GLOB_RECURSE _aerion_asio_hits LIST_DIRECTORIES FALSE
                     "${_root}/*/common/iasiodrv.h"
                     "${_root}/*/*/common/iasiodrv.h"
                     "${_root}/*/*/*/common/iasiodrv.h")
                if (_aerion_asio_hits)
                    list(GET _aerion_asio_hits 0 _aerion_asio_hit)
                    get_filename_component(_aerion_asio_common "${_aerion_asio_hit}" DIRECTORY)
                    break()
                endif()
            endif()
        endforeach()
    endif()

    if (_aerion_asio_common)
        # Gate the SDK include + JUCE_ASIO macro per build configuration. With
        # AERION_ENABLE_ASIO=AUTO (the default) the macro is enabled for ALL
        # configs, including Release - the SDK is GPLv3 so this is fine for our
        # GPL-licensed binaries. DEBUG_ONLY restores the previous gating.
        if (AERION_ENABLE_ASIO STREQUAL "DEBUG_ONLY")
            set(_aerion_asio_cfg_match "$<CONFIG:Debug>")
            message(STATUS "Aerion DAW: ASIO SDK found at ${_aerion_asio_common} - "
                           "enabling JUCE_ASIO for Debug builds only "
                           "(AERION_ENABLE_ASIO=DEBUG_ONLY).")
        else()
            set(_aerion_asio_cfg_match "1")
            message(STATUS "Aerion DAW: ASIO SDK found at ${_aerion_asio_common} - "
                           "enabling JUCE_ASIO for ALL configurations (Debug + Release). "
                           "Steinberg ASIO SDK is bundled under GPLv3 and the official "
                           "ASIO-compatible mark on splash (corner) + full notice in About box.")
        endif()

        target_compile_definitions(AerionDaw PRIVATE
            $<${_aerion_asio_cfg_match}:JUCE_ASIO=1>)
        target_include_directories(AerionDaw PRIVATE
            $<${_aerion_asio_cfg_match}:${_aerion_asio_common}>)
    else()
        message(STATUS "Aerion DAW: ASIO SDK not found - building without ASIO backend. "
                       "Drop the Steinberg ASIO SDK at AerionDawCpp/External/asiosdk or set "
                       "AERION_ASIO_SDK_DIR / ASIO_SDK_DIR to enable ASIO support.")
    endif()
elseif (WIN32)
    message(STATUS "Aerion DAW: AERION_ENABLE_ASIO=NEVER - ASIO backend disabled for all configurations.")
endif()

# ---- WinRT MIDI (Windows 10+) -------------------------------------------------
# Enables the modern Windows MIDI stack (Bluetooth-LE MIDI, hot-plug USB MIDI,
# multi-client). Requires the Windows 10/11 SDK, which ships with Visual Studio.
# Disabled by default in JUCE; we turn it on for Windows builds and let it
# silently no-op at runtime if the OS is too old.
if (WIN32)
    target_compile_definitions(AerionDaw PRIVATE JUCE_USE_WINRT_MIDI=1)
endif()

# ---- JACK (cross-platform pro-audio bus, opt-in) ------------------------------
# Used by many Linux producers and a subset of macOS users (Reaper, Ardour,
# Bitwig). Auto-detected: if the JACK headers + library are visible, we enable
# JUCE_JACK=1. On Windows JACK uses JackRouter; we additionally accept a header
# path via JACK_INCLUDE_DIR / JACK_LIBRARY for non-standard installs.
if (NOT WIN32)
    find_path(JACK_INCLUDE_DIR NAMES jack/jack.h
              HINTS /usr/include /usr/local/include /opt/homebrew/include)
    find_library(JACK_LIBRARY NAMES jack
                 HINTS /usr/lib /usr/local/lib /opt/homebrew/lib)
    if (JACK_INCLUDE_DIR AND JACK_LIBRARY)
        message(STATUS "Aerion DAW: JACK detected (${JACK_LIBRARY}) - enabling JUCE_JACK")
        target_compile_definitions(AerionDaw PRIVATE JUCE_JACK=1)
        target_include_directories(AerionDaw PRIVATE "${JACK_INCLUDE_DIR}")
        # JUCE loads libjack at runtime via dlopen, so we don't need to link it.
    else()
        message(STATUS "Aerion DAW: JACK headers not found - skipping JACK backend "
                       "(install jackd2/dev or set JACK_INCLUDE_DIR + JACK_LIBRARY).")
    endif()
endif()

# --- Packaging ----------------------------------------------------------------
# Shared CPack metadata for both NSIS (Windows) and DragNDrop (macOS). The
# version number is sourced from the `project(... VERSION)` declaration above so
# you only have to bump it in one place.
set(CPACK_PACKAGE_NAME             "Aerion DAW")
set(CPACK_PACKAGE_VENDOR           "Aethos Studio Ltd.")
set(CPACK_PACKAGE_VERSION          "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR    "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR    "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH    "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Aerion DAW - Pre-Alpha (v${PROJECT_VERSION})")
set(CPACK_PACKAGE_INSTALL_DIRECTORY   "Aerion DAW")
set(CPACK_PACKAGE_FILE_NAME           "AerionDAW-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_HOMEPAGE_URL        "https://aethos.studio")

# Whitelist only the AerionDaw sub-project for the installer payload. By
# default CPack runs install() rules for every CMake project under the build
# tree, which would drag in juceaide.exe, the full JUCE + Tracktion module
# headers, and a lot of test source. Restricting CPACK_INSTALL_CMAKE_PROJECTS
# to "AerionDaw / ALL / /" handles most of that; the pre-build script below
# scrubs anything that still slips through (the FetchContent sub-projects
# attach their install() rules to the parent build tree, so they bypass the
# whitelist).
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR}" "AerionDaw" "ALL" "/")
set(CPACK_PRE_BUILD_SCRIPTS
    "${CMAKE_CURRENT_SOURCE_DIR}/CMake/CPackPruneStaging.cmake")

if (WIN32)
    # JUCE wires the AerionDaw target's output to
    # <build>/AerionDaw_artefacts/<CFG>/Aerion DAW.exe via OUTPUT_NAME, so a
    # vanilla install(TARGETS) rule lands the .exe at the install root. CPack
    # then picks it up for the NSIS payload.
    install(TARGETS AerionDaw RUNTIME DESTINATION .)

    # Bundle only the small VC++ runtime DLLs (msvcp140.dll, vcruntime140.dll
    # etc.) that ship with the VC++ redistributable - users without it will
    # otherwise see a missing-DLL error on first launch. We deliberately do NOT
    # ship the UCRT (api-ms-win-*.dll) since those are guaranteed to be present
    # on Windows 10+ and adding them ~doubles the installer size.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
    include(InstallRequiredSystemLibraries)

    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME       "Aerion DAW")
    set(CPACK_NSIS_PACKAGE_NAME       "Aerion DAW")
    set(CPACK_NSIS_URL_INFO_ABOUT     "https://aethos.studio")
    set(CPACK_NSIS_HELP_LINK          "https://aethos.studio")
    set(CPACK_NSIS_CONTACT            "support@aethos.studio")
    set(CPACK_NSIS_MODIFY_PATH        ON)
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    # Start-menu + desktop shortcuts pointing at the installed exe.
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MENU_LINKS         "Aerion DAW.exe" "Aerion DAW")
    set(CPACK_PACKAGE_EXECUTABLES     "Aerion DAW" "Aerion DAW")
    set(CPACK_CREATE_DESKTOP_LINKS    "Aerion DAW")

    # Installer + uninstaller icon (must be a .ico).
    set(CPACK_NSIS_MUI_ICON           "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Aerion_Ico.ico")
    set(CPACK_NSIS_MUI_UNIICON        "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Aerion_Ico.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Aerion DAW.exe")

    include(CPack)
endif()

if (APPLE)
    # JUCE produces a "${PRODUCT_NAME}.app" bundle. install(TARGETS ... BUNDLE)
    # is the canonical CMake pattern - CPack's DragNDrop wraps whatever lands at
    # the install root into the .dmg payload.
    install(TARGETS AerionDaw BUNDLE DESTINATION .)

    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Aerion DAW ${PROJECT_VERSION}")

    include(CPack)
endif()
