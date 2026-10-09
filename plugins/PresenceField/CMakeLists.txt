juce_add_plugin(PresenceField
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE PrF1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Presence Field"
)

# Feature flag: PROCESSED (Option B) stereo-widener DSP.
# Default OFF  -> ships the current UNPROCESSED (Option A) path unchanged.
# Enable with  -DPRESENCEFIELD_PROCESSED=ON  to compile the PROCESSED variant.
option(PRESENCEFIELD_PROCESSED "Use the PROCESSED (Option B) PresenceField DSP" OFF)

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

if (PRESENCEFIELD_PROCESSED)
    target_compile_definitions(PresenceField
        PRIVATE
            PRESENCEFIELD_PROCESSED=1
    )
endif()

target_link_libraries(PresenceField
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# PresenceField Offline Render Tool
# Renders a REAL source audio file through the actual PresenceField DSP
# (PresenceFieldAudioProcessor::processBlock) to dry + processed WAVs for a
# genuine A/B (before/after) presentation.
#==============================================================================
juce_add_console_app(PresenceField_OfflineRender
    PRODUCT_NAME "PresenceField_OfflineRender"
    NEEDS_GUI FALSE
)

juce_generate_juce_header(PresenceField_OfflineRender)

target_sources(PresenceField_OfflineRender
    PRIVATE
        OfflineRender.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(PresenceField_OfflineRender
    PRIVATE
        Source
)

target_compile_definitions(PresenceField_OfflineRender
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="PresenceField"
        JucePlugin_Desc="Presence Field"
        JucePlugin_Manufacturer="Khri"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x50724631
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
        PRESENCE_FIELD_VERSION="0.1.0"
)

target_link_libraries(PresenceField_OfflineRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
