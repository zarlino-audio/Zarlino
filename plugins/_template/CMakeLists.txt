juce_add_plugin(TemplatePlugin
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE TMP1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Template Plugin"
)

target_sources(TemplatePlugin
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
)

target_compile_definitions(TemplatePlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TemplatePlugin
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
