# Reusable function for creating console test apps for Zarlino plugins
function(zarlino_add_plugin_test TEST_NAME PLUGIN_NAME PLUGIN_MANUFACTURER_CODE PLUGIN_CODE)
    juce_add_console_app(${TEST_NAME})
    
    juce_generate_juce_header(${TEST_NAME})
    
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests
        "${CMAKE_CURRENT_BINARY_DIR}/${PLUGIN_NAME}_artefacts/JuceLibraryCode"
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/shared/dsp
        ${CMAKE_SOURCE_DIR}/shared/ui
    )
    
    target_compile_definitions(${TEST_NAME} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="${PLUGIN_NAME}"
        JucePlugin_Desc="${PLUGIN_NAME}"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=${PLUGIN_MANUFACTURER_CODE}
        JucePlugin_PluginCode=${PLUGIN_CODE}
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
    
    target_link_libraries(${TEST_NAME} PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endfunction()
