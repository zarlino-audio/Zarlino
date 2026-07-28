juce_add_plugin(VocalToneShaper
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.vocaltoneshaper"
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
    PRIVATE
        VOCAL_TONE_SHAPER_VERSION="0.1.0"
)

target_link_libraries(VocalToneShaper
    PUBLIC
        zarlino_plugin_defaults
)
