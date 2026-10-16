#==============================================================================
# Zarlino Plugin Factory - deterministic factory kernel
#
# Targets
#   zarlino_factory          static library: spec model, registry, graph engine,
#                            parameter/macro algebra and the certified module
#                            library
#   Factory_Tests            factory-level automated test runner (CTest: Factory.Unit)
#   ZarlinoFactory_Check     CLI: load a PluginSpec from disk, validate it, build
#                            the graph and report the result
#==============================================================================
include(CTest)

#------------------------------------------------------------------------------
# Kernel + certified module library
#------------------------------------------------------------------------------
add_library(zarlino_factory STATIC
    src/Error.cpp
    src/Types.cpp
    src/JsonUtil.cpp
    src/ModuleManifest.cpp
    src/PluginSpec.cpp
    src/ModuleRegistry.cpp
    src/ParameterSystem.cpp
    src/GraphEngine.cpp

    modules/BuiltinModules.cpp
    modules/GainModule.cpp
    modules/FilterModule.cpp
    modules/TiltEqModule.cpp
    modules/ParametricEqModule.cpp
    modules/CompressorModule.cpp
    modules/GateModule.cpp
    modules/SaturationModule.cpp
    modules/LimiterModule.cpp
    modules/TransientShaperModule.cpp
    modules/StereoWidthModule.cpp
    modules/DryWetModule.cpp

    # Reuse of shipped DSP: the factory saturation module adapts ZScorch's
    # voiced, ADAA anti-aliased Saturator rather than re-implementing it.
    ${CMAKE_SOURCE_DIR}/plugins/ZScorch/Source/Saturator.cpp
)

target_include_directories(zarlino_factory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/shared/dsp
        ${CMAKE_SOURCE_DIR}/shared/utils
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/modules
        ${CMAKE_SOURCE_DIR}/plugins/ZScorch/Source
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
)

target_compile_definitions(zarlino_factory
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(zarlino_factory
    PUBLIC
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

#------------------------------------------------------------------------------
# Factory test runner
#------------------------------------------------------------------------------
juce_add_console_app(Factory_Tests)

juce_generate_juce_header(Factory_Tests)

target_sources(Factory_Tests
    PRIVATE
        tests/Main.cpp
        tests/SpecValidationTests.cpp
        tests/RegistryTests.cpp
        tests/GraphConstructionTests.cpp
        tests/ParameterMacroTests.cpp
        tests/AudioProcessingTests.cpp
        tests/DeterminismTests.cpp
        tests/SerializationTests.cpp
        tests/RealtimeSafetyTests.cpp
        tests/ExampleSpecTests.cpp
)

target_include_directories(Factory_Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    # Shared Zarlino test stimulus generators (ZStressTestUtils), so the factory
    # suite exercises the same analytic and hostile signals as the plugin suites.
    ${CMAKE_SOURCE_DIR}/shared/testing
)

# The factory tests load the committed example PluginSpecs from disk, so the CI
# run exercises the same documents a user or the next engineering slice will.
target_compile_definitions(Factory_Tests PRIVATE
    ZFACTORY_SPEC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/specs"
)

target_link_libraries(Factory_Tests
    PRIVATE
        zarlino_factory
        juce::juce_gui_basics
        juce::juce_audio_processors   # for shared/testing/ZStressTestUtils.h
)

if(BUILD_TESTING)
    add_test(NAME Factory.Unit COMMAND Factory_Tests)
    set_tests_properties(Factory.Unit PROPERTIES LABELS "Factory;DSP;release-gate")
endif()

#------------------------------------------------------------------------------
# CLI: validate and build a PluginSpec from the command line
#------------------------------------------------------------------------------
juce_add_console_app(ZarlinoFactory_Check)

juce_generate_juce_header(ZarlinoFactory_Check)

target_sources(ZarlinoFactory_Check PRIVATE tools/FactoryCheck.cpp)

target_link_libraries(ZarlinoFactory_Check
    PRIVATE
        zarlino_factory
        juce::juce_gui_basics
)
