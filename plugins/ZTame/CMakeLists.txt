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
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.2.0"
)

target_link_libraries(ZTame
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Minimal test to isolate hang
juce_add_console_app(ZTame_MinimalTest)

target_sources(ZTame_MinimalTest
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/MinimalTest.cpp
)

juce_generate_juce_header(ZTame_MinimalTest)

target_include_directories(ZTame_MinimalTest
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_MinimalTest
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_MinimalTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
#==============================================================================
# Diagnostic test for isolating issues
juce_add_console_app(ZTame_Diagnostic)

target_sources(ZTame_Diagnostic
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/DiagnosticTest.cpp
)

juce_generate_juce_header(ZTame_Diagnostic)

target_include_directories(ZTame_Diagnostic
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_Diagnostic
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_Diagnostic
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app(ZTame_Tests)

target_sources(ZTame_Tests
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/TestMain.cpp
        Tests/CorrectnessTests.cpp
        Tests/FuzzTests.cpp
        Tests/PluginLevelTests.cpp
)

juce_generate_juce_header(ZTame_Tests)

target_include_directories(ZTame_Tests
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_Tests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_Tests
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Offline A/B render lab: writes dry / processed / removed WAVs for a set of
# demo sources so the before-after difference can be judged by ear.
juce_add_console_app(ZTame_RenderLab)

target_sources(ZTame_RenderLab
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/RenderLab.cpp
)

juce_generate_juce_header(ZTame_RenderLab)

target_include_directories(ZTame_RenderLab
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_RenderLab
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.2.0"
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_RenderLab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Real-instrument A/B renderer.
#
# Unlike ZTame_RenderLab (which synthesises its demo material), this target reads
# REAL recorded multitrack stems from disk, renders each through the actual engine
# as an aligned dry/wet pair, loudness-matches the wet file to the dry one, and
# reports peak dBFS + integrated LUFS for every file. Output is 24-bit / 48 kHz.
juce_add_console_app(ZTame_RealRender)

target_sources(ZTame_RealRender
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/RealInstrumentRender.cpp
)

juce_generate_juce_header(ZTame_RealRender)

target_include_directories(ZTame_RealRender
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_RealRender
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.2.0"
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_RealRender
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Offscreen editor snapshot: renders the real editor to a PNG for layout review.
juce_add_console_app(ZTame_UiShot)

target_sources(ZTame_UiShot
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/UiShot.cpp
)

juce_generate_juce_header(ZTame_UiShot)

target_include_directories(ZTame_UiShot
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_UiShot
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.2.0"
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_UiShot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Engine performance / calibration lab.
#
# Measures processBlock cost (ns/sample, % of realtime) for the active engine,
# the idle fast path, the silence fast path and bypass, plus the suppression
# depth and collateral change on a synthetic resonance. Plain-text output, so
# the release CPU report can be produced with:
#
#   ZTame_PerfLab.exe > artifacts/ztame-v1.2.0/cpu-report.txt
juce_add_console_app(ZTame_PerfLab)

target_sources(ZTame_PerfLab
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Tests/PerfLab.cpp
        Tests/V11Engine.cpp
)

juce_generate_juce_header(ZTame_PerfLab)

target_include_directories(ZTame_PerfLab
    PRIVATE
        Source
        Tests
        "${CMAKE_CURRENT_BINARY_DIR}/ZTame_artefacts/JuceLibraryCode"
)

target_compile_definitions(ZTame_PerfLab
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        ZTAME_VERSION="1.2.0"
        JucePlugin_Name="ZTame"
        JucePlugin_Desc="ZTame"
        JucePlugin_Manufacturer="Zarlino Audio"
        JucePlugin_ManufacturerCode=0x4b687269
        JucePlugin_PluginCode=0x5a546d31
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

target_link_libraries(ZTame_PerfLab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#==============================================================================
# Test registration (CTest). The DSP suite is the release gate:
#   ctest --test-dir build -C Release -R ZTame
#   ctest --test-dir build -C Release -L release-gate
include(CTest)
if (BUILD_TESTING)
    add_test(NAME ZTame.Unit COMMAND ZTame_Tests)
    set_tests_properties(ZTame.Unit PROPERTIES LABELS "ZTame;DSP;release-gate")
endif()

#==============================================================================
# Bundled assets
#
# The footer crest is embedded in the binary (juce_add_binary_data) rather than
# read from disk, so the plugin has no runtime file dependencies.
juce_add_binary_data(ZTame_Assets
    SOURCES
        Assets/zarlino-logo.svg
)

#==============================================================================
# Source text encoding.
#
# The UI strings and some test names contain non-ASCII characters (typographic
# quotes, arrows, the plus-minus sign). With no explicit flag MSVC guesses the
# source encoding from the bytes on disk and, on a machine whose active code page
# is not UTF-8, that guess produces mojibake in the shipped binary. Every literal
# in this plugin is authored as UTF-8, so the compiler is told exactly that.
if (MSVC)
    target_compile_options(ZTame            PRIVATE /utf-8)
    target_compile_options(ZTame_MinimalTest PRIVATE /utf-8)
    target_compile_options(ZTame_Diagnostic  PRIVATE /utf-8)
    target_compile_options(ZTame_Tests       PRIVATE /utf-8)
    target_compile_options(ZTame_RenderLab   PRIVATE /utf-8)
    target_compile_options(ZTame_RealRender  PRIVATE /utf-8)
    target_compile_options(ZTame_UiShot      PRIVATE /utf-8)
    target_compile_options(ZTame_PerfLab     PRIVATE /utf-8)
endif()

# The editor and the shared design system are included with repo-root-relative
# paths ("shared/ui/theme/ZarlinoLookAndFeel.h"), so the repository root must be
# on the include path of every target that compiles the editor.
foreach (ztame_target ZTame ZTame_MinimalTest ZTame_Diagnostic ZTame_Tests ZTame_RenderLab ZTame_RealRender ZTame_UiShot ZTame_PerfLab)
    target_include_directories(${ztame_target} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${ztame_target} PRIVATE ZTame_Assets)
endforeach()
