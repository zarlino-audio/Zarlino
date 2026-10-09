#==============================================================================
# ZScorch v2.0 — adaptive multi-band harmonic processor
#==============================================================================
include(${CMAKE_SOURCE_DIR}/cmake/ZarlinoTest.cmake)

# Shared licensing (ZLicenseManager + embedded public key). The shared tree is
# not added globally, so pull the interface library in on demand.
if(NOT TARGET zarlino_licensing)
    add_subdirectory(${CMAKE_SOURCE_DIR}/shared/licensing
                     ${CMAKE_BINARY_DIR}/zarlino_licensing)
endif()

juce_add_plugin(ZScorch
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.zscorch"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZSc1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZScorch"
)

juce_generate_juce_header(ZScorch)

target_sources(ZScorch
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
)

target_include_directories(ZScorch
    PRIVATE
        Source
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/shared/dsp
        ${CMAKE_SOURCE_DIR}/shared/ui
        ${CMAKE_SOURCE_DIR}/shared/licensing
)

target_compile_definitions(ZScorch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZSCORCH_VERSION="2.0.0"
)

target_link_libraries(ZScorch
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        zarlino_licensing
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# ZScorch v2.0 test runner (DSP correctness, presets, robustness, fuzz)
zarlino_add_plugin_test(ZScorch_Tests ZScorch 0x4b687269 0x5a534331)

target_sources(ZScorch_Tests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
        Tests/Main.cpp
        Tests/SmokeTest.cpp
        Tests/SaturatorTests.cpp
        Tests/ExciterTests.cpp
        Tests/TiltTests.cpp
        Tests/ControlLayerTests.cpp
        Tests/AnalysisCoreTests.cpp
        Tests/PluginLevelTests.cpp
        Tests/RobustnessTests.cpp
        Tests/AutomationStressTests.cpp
        Tests/FuzzTests.cpp
        Tests/LicenseTests.cpp
)

target_link_libraries(ZScorch_Tests PRIVATE
    zarlino_licensing
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ZScorch.Unit COMMAND ZScorch_Tests)
    set_tests_properties(ZScorch.Unit PROPERTIES LABELS "ZScorch;DSP;release-gate")
endif()
