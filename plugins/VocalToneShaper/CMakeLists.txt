juce_add_plugin(VocalToneShaper
    COMPANY_NAME "Khritical Research"
    COMPANY_WEBSITE "https://khriticalresearch.local"
    BUNDLE_ID "com.khriticalresearch.vocaltoneshaper"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE VtS2
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "Vocal Tone Shaper"
)

juce_generate_juce_header(VocalToneShaper)

target_sources(VocalToneShaper
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(VocalToneShaper
    PRIVATE
        Source
)

target_compile_definitions(VocalToneShaper
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        VOCAL_TONE_SHAPER_VERSION="0.1.0"
)

target_link_libraries(VocalToneShaper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
