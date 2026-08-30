juce_add_plugin(ZTame
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.ztame"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZTm1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZTame"
)

juce_generate_juce_header(ZTame)

target_sources(ZTame
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZTame
    PRIVATE
        Source
)

target_compile_definitions(ZTame
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.0.0"
)

target_link_libraries(ZTame
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Minimal test to isolate hang
juce_add_console_app(ZTame_MinimalTest)

target_sources(ZTame_MinimalTest
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/MinimalTest.cpp
)

juce_generate_juce_header(ZTame_MinimalTest)

target_include_directories(ZTame_MinimalTest
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_MinimalTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_MinimalTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
#==============================================================================
# Diagnostic test for isolating issues
juce_add_console_app(ZTame_Diagnostic)

target_sources(ZTame_Diagnostic
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/DiagnosticTest.cpp
)

juce_generate_juce_header(ZTame_Diagnostic)

target_include_directories(ZTame_Diagnostic
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_Diagnostic
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_Diagnostic
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(ZTame_Tests)

target_sources(ZTame_Tests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/TestMain.cpp
        Tests/CorrectnessTests.cpp
        Tests/FuzzTests.cpp
        Tests/PluginLevelTests.cpp
)

juce_generate_juce_header(ZTame_Tests)

target_include_directories(ZTame_Tests
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_Tests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_Tests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Offline A/B render tool — processes real audio files through the actual
# ZTame DSP (dry + processed) for the website demos.
#   ZTame_OfflineRender <in.wav|.ogg> <dryOut.wav> <procOut.wav> <preset>
juce_add_console_app(ZTame_OfflineRender)

target_sources(ZTame_OfflineRender
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/OfflineRender.cpp
)

juce_generate_juce_header(ZTame_OfflineRender)

target_include_directories(ZTame_OfflineRender
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_OfflineRender
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_OfflineRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
