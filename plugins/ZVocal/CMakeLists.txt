juce_add_plugin(ZVocal
    COMPANY_NAME "Khritical Research"
    COMPANY_WEBSITE "https://khriticalresearch.local"
    BUNDLE_ID "com.khriticalresearch.zvocal"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE Zvcl
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZVocal"
    VERSION 0.2.0
)

juce_generate_juce_header(ZVocal)

target_sources(ZVocal
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZVocal
    PRIVATE
        Source
)

target_compile_definitions(ZVocal
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZVOCAL_VERSION="0.2.0"
)

target_link_libraries(ZVocal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# DSP correctness + fuzz runner (console app). Run the exe directly:
#   build/plugins/ZVocal/ZVocal_Tests_artefacts/Release/ZVocal_Tests.exe
juce_add_console_app(ZVocal_Tests)

target_sources(ZVocal_Tests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/TestMain.cpp
        Tests/CorrectnessTests.cpp
        Tests/FuzzTests.cpp
)

juce_generate_juce_header(ZVocal_Tests)

target_include_directories(ZVocal_Tests
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZVocal_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZVocal_Tests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZVocal"
        JucePlugin_Desc="ZVocal"
        JucePlugin_Manufacturer="Khritical Research"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a76636c
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

target_link_libraries(ZVocal_Tests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Register the runner with CTest so it can act as a gate (same shape as ZScorch):
#   ctest --test-dir build -C Release -R ZVocal
#   ctest --test-dir build -C Release -L release-gate
include(CTest)
if (BUILD_TESTING)
    add_test(NAME ZVocal.Unit COMMAND ZVocal_Tests)
    set_tests_properties(ZVocal.Unit PROPERTIES LABELS "ZVocal;DSP;release-gate")
endif()
