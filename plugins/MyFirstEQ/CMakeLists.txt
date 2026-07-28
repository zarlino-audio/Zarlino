juce_add_plugin(MyFirstEQ
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.myfirsteq"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE EQ01
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "My First EQ"
)

target_sources(MyFirstEQ
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(MyFirstEQ
    PRIVATE
        MYFIRSTEQ_VERSION="0.1.0"
)

target_link_libraries(MyFirstEQ
    PUBLIC
        zarlino_plugin_defaults
)
