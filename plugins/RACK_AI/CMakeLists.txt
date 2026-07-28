juce_add_plugin(RACK_AI
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.rack_ai"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE RAI1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
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
    PRIVATE
        RACK_AI_VERSION="1.0.0"
)

target_link_libraries(RACK_AI
    PUBLIC
        zarlino_plugin_defaults
    PRIVATE
        juce::juce_core
        juce::juce_cryptography
        zarlino_shared
)

# RACK_AI needs curl — override the default INTERFACE setting
target_compile_definitions(RACK_AI PRIVATE JUCE_USE_CURL=1)
