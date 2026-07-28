cmake_minimum_required(VERSION 3.22)
project(VSTWorkspace VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Point JUCE at the VST3 SDK
set(JUCE_VST3_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sdk/VST3_SDK")

# JUCE should be cloned into this workspace before building.
add_subdirectory(JUCE)

# Shared plugin defaults — all Zarlino plugins inherit these
add_library(zarlino_plugin_defaults INTERFACE)
target_compile_definitions(zarlino_plugin_defaults INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(zarlino_plugin_defaults INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Optional shared library containing reusable DSP/UI code.
add_subdirectory(shared)

# Plugin projects
add_subdirectory(plugins/MyFirstEQ)
add_subdirectory(plugins/RACK_AI)
add_subdirectory(plugins/PresenceField)
add_subdirectory(plugins/VocalToneShaper)
add_subdirectory(plugins/ZTame)
add_subdirectory(plugins/ZAdaptiveHarmonics)
