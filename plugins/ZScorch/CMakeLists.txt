juce_add_plugin(ZScorch
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.zscorch"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZSc1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZScorch"
)

juce_generate_juce_header(ZScorch)

target_sources(ZScorch
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZScorch
    PRIVATE
        Source
)

target_compile_definitions(ZScorch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZSCORCH_VERSION="1.0.0"
)

target_link_libraries(ZScorch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# ZScorch DSP Correctness & Fuzz Test Runner
# Temporarily disabled — test source files not yet created
# juce_add_console_app(ZScorch_Tests)
#
# target_sources(ZScorch_Tests
#     PRIVATE
#         Source/PluginProcessor.cpp
#         Source/PluginEditor.cpp
#         Tests/TestMain.cpp
#         Tests/SaturationTests.cpp
#         Tests/OversamplingTests.cpp
#         Tests/CrossoverTests.cpp
#         Tests/EQTests.cpp
#         Tests/CeilingLimiterTests.cpp
#         Tests/FullChainTests.cpp
# )
#
# juce_generate_juce_header(ZScorch_Tests)
#
# target_include_directories(ZScorch_Tests
#     PRIVATE
#         Source
#         Tests
#         "${CMAKE_CURRENT_BINARY_DIR}/ZScorch_artefacts/JuceLibraryCode"
# )
#
# target_compile_definitions(ZScorch_Tests
#     PRIVATE
#         JUCE_WEB_BROWSER=0
#         JUCE_USE_CURL=0
#         JUCE_VST3_CAN_REPLACE_VST2=0
#         JucePlugin_Name="ZScorch"
#         JucePlugin_Desc="ZScorch"
#         JucePlugin_Manufacturer="Zarlino Audio"
#         JucePlugin_ManufacturerCode=0x4b687269
#         JucePlugin_PluginCode=0x5a536331
#         JucePlugin_IsSynth=0
#         JucePlugin_Build_VST3=0
#         JucePlugin_Build_Standalone=0
#         JucePlugin_Build_AU=0
#         JucePlugin_Build_AAX=0
#         JucePlugin_Build_LV2=0
#         JucePlugin_Build_Unity=0
#         JucePlugin_Build_VST=0
#         JUCE_SHARED_CODE=1
# )
#
# target_link_libraries(ZScorch_Tests
#     PRIVATE
#         juce::juce_audio_utils
#         juce::juce_dsp
#     PUBLIC
#         juce::juce_recommended_config_flags
#         juce::juce_recommended_warning_flags
# )