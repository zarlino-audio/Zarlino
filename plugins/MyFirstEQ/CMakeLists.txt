juce_add_plugin(MyFirstEQ
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE EQ01
    FORMATS VST3 Standalone
    PRODUCT_NAME "My First EQ"
)

target_sources(MyFirstEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(MyFirstEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MyFirstEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
