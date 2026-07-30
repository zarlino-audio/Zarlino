juce_add_plugin(ZAdaptiveHarmonics
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.zadaptiveharmonics"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZAH1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZAdaptiveHarmonics"
)

juce_generate_juce_header(ZAdaptiveHarmonics)

target_sources(ZAdaptiveHarmonics
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
)

target_include_directories(ZAdaptiveHarmonics
    PRIVATE
        Source
)

target_compile_definitions(ZAdaptiveHarmonics
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZADAPTIVEHARMONICS_VERSION="1.0.0"
)

target_link_libraries(ZAdaptiveHarmonics
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Minimal smoke test
juce_add_console_app(ZAdaptiveHarmonics_MinimalTest)

target_sources(ZAdaptiveHarmonics_MinimalTest
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
        Tests/MinimalTest.cpp
)

juce_generate_juce_header(ZAdaptiveHarmonics_MinimalTest)

target_include_directories(ZAdaptiveHarmonics_MinimalTest
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZAdaptiveHarmonics_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZAdaptiveHarmonics_MinimalTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZAdaptiveHarmonics"
        JucePlugin_Desc="ZAdaptiveHarmonics"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a414831
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
)

target_link_libraries(ZAdaptiveHarmonics_MinimalTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# DSP correctness tests
juce_add_console_app(ZAdaptiveHarmonics_CorrectnessTests)

target_sources(ZAdaptiveHarmonics_CorrectnessTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
        Tests/TestMain.cpp
        Tests/CorrectnessTests.cpp
)

juce_generate_juce_header(ZAdaptiveHarmonics_CorrectnessTests)

target_include_directories(ZAdaptiveHarmonics_CorrectnessTests
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZAdaptiveHarmonics_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZAdaptiveHarmonics_CorrectnessTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZAdaptiveHarmonics"
        JucePlugin_Desc="ZAdaptiveHarmonics"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a414831
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
)

target_link_libraries(ZAdaptiveHarmonics_CorrectnessTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Plugin-level integration tests
juce_add_console_app(ZAdaptiveHarmonics_PluginLevelTests)

target_sources(ZAdaptiveHarmonics_PluginLevelTests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
        Tests/TestMain.cpp
        Tests/PluginLevelTests.cpp
)

juce_generate_juce_header(ZAdaptiveHarmonics_PluginLevelTests)

target_include_directories(ZAdaptiveHarmonics_PluginLevelTests
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZAdaptiveHarmonics_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZAdaptiveHarmonics_PluginLevelTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZAdaptiveHarmonics"
        JucePlugin_Desc="ZAdaptiveHarmonics"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a414831
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
)

target_link_libraries(ZAdaptiveHarmonics_PluginLevelTests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Performance smoke test
juce_add_console_app(ZAdaptiveHarmonics_PerfSmoke)

target_sources(ZAdaptiveHarmonics_PerfSmoke
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
        Tests/PerformanceSmokeTest.cpp
)

juce_generate_juce_header(ZAdaptiveHarmonics_PerfSmoke)

target_include_directories(ZAdaptiveHarmonics_PerfSmoke
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZAdaptiveHarmonics_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZAdaptiveHarmonics_PerfSmoke
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZAdaptiveHarmonics"
        JucePlugin_Desc="ZAdaptiveHarmonics"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a414831
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
)

target_link_libraries(ZAdaptiveHarmonics_PerfSmoke
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
