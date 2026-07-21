juce_add_plugin(RACK_AI
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE RAI1
    FORMATS VST3 Standalone
    PRODUCT_NAME "RACK/AI"
)

target_sources(RACK_AI
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AudioAnalysisEngine.cpp
        Source/ParameterControl.cpp
        Source/AIIntegration.cpp
        Source/ChangeLog.cpp
        Source/UIComponents/SlotList.cpp
        Source/UIComponents/AIChat.cpp
        Source/UIComponents/AnalysisOverlay.cpp
        Source/UIComponents/ChangeLogPanel.cpp
)

target_include_directories(RACK_AI
    PRIVATE
        Source
        Source/UIComponents
)

target_compile_definitions(RACK_AI
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        RACK_AI_VERSION="1.0.0"
)

target_link_libraries(RACK_AI
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_core
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
