include(${CMAKE_SOURCE_DIR}/cmake/ZarlinoTest.cmake)

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
        Source/AnalysisCore.cpp
        Source/ControlLayer.cpp
        Source/Saturator.cpp
        Source/Tilt.cpp
        Source/Exciter.cpp
)

target_include_directories(ZAdaptiveHarmonics
    PRIVATE
        Source
)

target_compile_definitions(ZAdaptiveHarmonics
    PRIVATE
        ZADAPTIVEHARMONICS_VERSION="1.0.0"
)

target_link_libraries(ZAdaptiveHarmonics
    PUBLIC
        zarlino_plugin_defaults
)

#==============================================================================
# Console test runner
zarlino_add_plugin_test(ZAdaptiveHarmonics_Tests ZAdaptiveHarmonics 0x4b687269 0x5a414831)

target_sources(ZAdaptiveHarmonics_Tests
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
        Tests/ControlLayerTests.cpp
        Tests/SaturatorTests.cpp
        Tests/TiltTests.cpp
        Tests/ExciterTests.cpp
        Tests/AnalysisCoreTests.cpp
        Tests/FuzzTests.cpp
        Tests/PluginLevelTests.cpp
)

# (Includes/compile/link for the test target are handled by zarlino_add_plugin_test)

