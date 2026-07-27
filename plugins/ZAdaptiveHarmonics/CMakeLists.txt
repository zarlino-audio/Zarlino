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
