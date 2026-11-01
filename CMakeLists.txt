cmake_minimum_required(VERSION 3.22)
project(VocalTune VERSION 1.0.0 LANGUAGES C CXX)

add_compile_definitions(JUCE_USE_MP3AUDIOFORMAT=1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# -----------------------------------------------------------------------------
# DSP core (no JUCE dependency, also compiled into the console test binary)
# -----------------------------------------------------------------------------
add_library(VocalTuneCore STATIC
    Source/Core/PitchDetector.cpp
    Source/Core/GranularShifter.cpp
    Source/Core/NoteQuantizer.cpp)
target_include_directories(VocalTuneCore PUBLIC Source)
target_compile_features(VocalTuneCore PUBLIC cxx_std_20)
if (MSVC)
    target_compile_options(VocalTuneCore PRIVATE /utf-8)
endif()

add_executable(VocalTuneTests Tests/CoreTests.cpp)
target_link_libraries(VocalTuneTests PRIVATE VocalTuneCore)
enable_testing()
add_test(NAME CoreTests COMMAND VocalTuneTests)

add_executable(OfflineRenderTests
    Tests/OfflineRenderTests.cpp
    Source/OfflineRenderer.cpp)
target_link_libraries(OfflineRenderTests PRIVATE
    VocalTuneCore
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if (MSVC)
    target_compile_options(OfflineRenderTests PRIVATE /utf-8)
endif()
add_test(NAME OfflineRenderTests COMMAND OfflineRenderTests)
set_tests_properties(OfflineRenderTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(PitchViewTests
    Tests/PitchViewTests.cpp
    Source/PitchView.cpp
    Source/AudioEngine.cpp
    Source/Recorder.cpp)
target_link_libraries(PitchViewTests PRIVATE
    VocalTuneCore
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if (MSVC)
    target_compile_options(PitchViewTests PRIVATE /utf-8)
endif()
add_test(NAME PitchViewTests COMMAND PitchViewTests)
set_tests_properties(PitchViewTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(AudioFilePanelTests
    Tests/AudioFilePanelTests.cpp
    Source/AudioFilePanel.cpp
    Source/AudioPreviewPlayer.cpp
    Source/OfflineRenderer.cpp
    Source/StudioLookAndFeel.cpp)
target_link_libraries(AudioFilePanelTests PRIVATE
    VocalTuneCore
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
if (MSVC)
    target_compile_options(AudioFilePanelTests PRIVATE /utf-8)
endif()
add_test(NAME AudioFilePanelTests COMMAND AudioFilePanelTests)
set_tests_properties(AudioFilePanelTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# -----------------------------------------------------------------------------
# Application
# -----------------------------------------------------------------------------
juce_add_binary_data(VocalTuneAssets SOURCES
    Source/Resources/overlay.html)

juce_add_gui_app(VocalTune
    PRODUCT_NAME "Vocal Tune"
    COMPANY_NAME "Local"
    VERSION "1.0.0"
    BUNDLE_ID "com.local.vocaltune"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resources/icon-big.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Source/Resources/icon-small.png")

target_sources(VocalTune PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioFilePanel.cpp
    Source/AudioPreviewPlayer.cpp
    Source/PitchView.cpp
    Source/UiComponents.cpp
    Source/StudioLookAndFeel.cpp
    Source/AudioEngine.cpp
    Source/OfflineRenderer.cpp
    Source/OverlayServer.cpp
    Source/Recorder.cpp)

target_compile_definitions(VocalTune PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:VocalTune,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:VocalTune,JUCE_VERSION>")

target_link_libraries(VocalTune PRIVATE
    VocalTuneCore
    VocalTuneAssets
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

if (MSVC)
    target_compile_options(VocalTune PRIVATE /utf-8 /MP)
endif()

if (WIN32)
    target_link_libraries(VocalTune PRIVATE dwmapi)
endif()

set_target_properties(VocalTune PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
