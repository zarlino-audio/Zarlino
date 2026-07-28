juce_add_plugin(TemplatePlugin
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.templateplugin"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE TMP1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "Template Plugin"
)

target_sources(TemplatePlugin
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
)

target_compile_definitions(TemplatePlugin
    PRIVATE
        TEMPLATE_VERSION="0.1.0"
)

target_link_libraries(TemplatePlugin
    PUBLIC
        zarlino_plugin_defaults
)
