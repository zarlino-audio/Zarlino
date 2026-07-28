include(${CMAKE_SOURCE_DIR}/cmake/ZarlinoTest.cmake)

juce_add_plugin(ZTame
    COMPANY_NAME "Zarlino Audio"
    COMPANY_WEBSITE "https://zarlinoaudio.local"
    BUNDLE_ID "com.zarlinoaudio.ztame"
    PLUGIN_MANUFACTURER_CODE Khri
    PLUGIN_CODE ZTm1
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "ZTame"
)

juce_generate_juce_header(ZTame)

target_sources(ZTame
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(ZTame
    PRIVATE
        Source
)

target_compile_definitions(ZTame
    PRIVATE
        ZTAME_VERSION="1.0.0"
)

target_link_libraries(ZTame
    PUBLIC
        zarlino_plugin_defaults
)

zarlino_add_plugin_test(ZTame_Tests ZTame 0x4b687269 0x5a546d31)

target_sources(ZTame_Tests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/TestMain.cpp
        Tests/CorrectnessTests.cpp
        Tests/FuzzTests.cpp
        Tests/PluginLevelTests.cpp
        Tests/MinimalTest.cpp
        Tests/DiagnosticTest.cpp
)

# (Includes/compile/link for the test target are handled by zarlino_add_plugin_test)

