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

# --- Packaging ---
if (WIN32)
    install(TARGETS AerionDaw DESTINATION .)
    
    set(CPACK_PACKAGE_NAME "Aerion DAW")
    set(CPACK_PACKAGE_VENDOR "Aethos Studio Ltd.")
    set(CPACK_PACKAGE_VERSION "0.2.0")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Aerion DAW Pre-Alpha")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Aerion DAW")
    set(CPACK_GENERATOR "NSIS")
    
    # NSIS specifics
    set(CPACK_NSIS_DISPLAY_NAME "Aerion DAW")
    set(CPACK_NSIS_PACKAGE_NAME "Aerion DAW")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://aethos.studio")
    set(CPACK_NSIS_MODIFY_PATH ON)

    # Create shortcuts
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MENU_LINKS "Aerion DAW.exe" "Aerion DAW")
    set(CPACK_CREATE_DESKTOP_LINKS "Aerion DAW")

    # Installer Icon
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Aerion_Ico.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Aerion_Ico.ico")
    
    include(CPack)
endif()
