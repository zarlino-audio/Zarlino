juce_add_plugin(PresenceField
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE PrF1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Presence Field"
)

juce_generate_juce_header(PresenceField)

target_sources(PresenceField
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(PresenceField
    PRIVATE
        Source
)

target_compile_definitions(PresenceField
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        PRESENCE_FIELD_VERSION="0.1.0"
)

target_link_libraries(PresenceField
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
