juce_add_plugin(ZPunch
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZPn1
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZPunch"
)

juce_generate_juce_header(ZPunch)

target_sources(ZPunch
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZPunch
    PRIVATE
        Source
)

target_compile_definitions(ZPunch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZPUNCH_VERSION="0.1.0"
)

target_link_libraries(ZPunch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
