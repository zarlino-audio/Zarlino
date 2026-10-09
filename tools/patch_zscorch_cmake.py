import io

path = r'C:/Users/khrit/Desktop/plugin/plugins/ZScorch/CMakeLists.txt'
with io.open(path, 'r', encoding='utf-8', newline='') as f:
    src = f.read()

app = '''
#==============================================================================
# ZScorch Offline Render Tool
# Renders a REAL source audio file through the actual ZScorch DSP
# (ZScorchAudioProcessor::processBlock) to dry + processed WAVs for a
# genuine A/B (before/after) presentation of the re-engineered DSP.
#==============================================================================
juce_add_console_app(ZScorch_OfflineRender
    PRODUCT_NAME "ZScorch_OfflineRender"
    NEEDS_GUI FALSE
)

juce_generate_juce_header(ZScorch_OfflineRender)

target_sources(ZScorch_OfflineRender
    PRIVATE
        OfflineRender.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZScorch_OfflineRender
    PRIVATE
        Source
)

target_compile_definitions(ZScorch_OfflineRender
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZScorch"
        JucePlugin_Desc="ZScorch"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a536331
        JucePlugin_IsSynth=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_LV2=0
        JucePlugin_Build_Unity=0
        JucePlugin_Build_VST=0
        JUCE_SHARED_CODE=1
)

target_link_libraries(ZScorch_OfflineRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
'''
src += app

with io.open(path, 'w', encoding='utf-8', newline='') as f:
    f.write(src)
print('OK appended ZScorch_OfflineRender target to CMakeLists')
