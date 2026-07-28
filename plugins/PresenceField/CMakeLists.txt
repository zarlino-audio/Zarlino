juce_add_plugin(PresenceField
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.presencefield"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE PrF1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
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
    PRIVATE
        PRESENCE_FIELD_VERSION="0.1.0"
)

target_link_libraries(PresenceField
    PUBLIC
        zarlino_plugin_defaults
)
