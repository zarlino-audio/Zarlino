#==============================================================================
# Zarlino Plugin Factory - deterministic factory kernel
#
# Targets
#   zarlino_factory          static library: spec model, registry, graph engine,
#                            parameter/macro algebra, UI compiler and the
#                            certified module library
#   Factory_Tests            factory-level automated test runner (CTest: Factory.Unit)
#   ZarlinoFactory_Check     CLI: load a PluginSpec from disk, validate it, build
#                            the graph and report the result
#   ZarlinoFactory_Generate  CLI: turn a PluginSpec into a buildable product project
#
# This directory is also consumed as a subdirectory by the generated product
# projects (see factory/shell/ShellTargets.cmake), so it never assumes it owns
# the top of the source tree: the monorepo root is ZARLINO_ROOT, which an
# embedding project sets before add_subdirectory().
#==============================================================================
include(CTest)

# The monorepo root, i.e. the checkout that owns JUCE/, shared/ and plugins/.
if(NOT DEFINED ZARLINO_ROOT)
    get_filename_component(ZARLINO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

# A caller may pass a native Windows path (D:\a\Zarlino\Zarlino). CMake parses
# backslashes in arguments as escape sequences, so a path segment such as \a or
# \t silently becomes an invalid escape and the whole configure fails. Normalise
# once, here, for every consumer.
file(TO_CMAKE_PATH "${ZARLINO_ROOT}" ZARLINO_ROOT)

# The factory's own test runner and CLIs are part of the factory's gate, not of
# any product that embeds the kernel. An embedding product switches them off.
option(ZFACTORY_BUILD_TOOLS "Build the factory test runner and command line tools" ON)

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
    src/UiPlan.cpp

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
    modules/DelayModule.cpp
    modules/ReverbModule.cpp
    modules/ChorusModule.cpp
    modules/FlangerModule.cpp
    modules/PhaserModule.cpp
    modules/TremoloModule.cpp
    modules/MidSideModule.cpp
    modules/ClipperModule.cpp
    modules/ExciterModule.cpp
    modules/DeEsserModule.cpp

    # Reuse of shipped DSP: the factory saturation module adapts ZScorch's
    # voiced, ADAA anti-aliased Saturator rather than re-implementing it.
    ${ZARLINO_ROOT}/plugins/ZScorch/Source/Saturator.cpp
)

target_include_directories(zarlino_factory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ZARLINO_ROOT}/shared/dsp
        ${ZARLINO_ROOT}/shared/utils
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/modules
        ${ZARLINO_ROOT}/plugins/ZScorch/Source
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
# Factory test runner and command line tools. A generated product embeds the
# kernel but not its gate, so these are skipped when the factory is a
# subdirectory of a product project.
#------------------------------------------------------------------------------
if(ZFACTORY_BUILD_TOOLS)

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
        tests/UiPlanTests.cpp
        tests/ModulePrimitivesTests.cpp
)

target_include_directories(Factory_Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
    # Shared Zarlino test stimulus generators (ZStressTestUtils), so the factory
    # suite exercises the same analytic and hostile signals as the plugin suites.
    ${ZARLINO_ROOT}/shared/testing
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

#------------------------------------------------------------------------------
# CLI: turn a PluginSpec into a buildable product project
#------------------------------------------------------------------------------
juce_add_console_app(ZarlinoFactory_Generate)

juce_generate_juce_header(ZarlinoFactory_Generate)

target_sources(ZarlinoFactory_Generate PRIVATE
    tools/Generate.cpp
    # The generator and the plugin shell share the product identity helpers and
    # the provenance record type, so a generated project and the binary built
    # from it can never disagree about what they are. ProductRuntime.cpp is
    # deliberately NOT linked here: it parses a product's embedded payload, which
    # a generator must not need.
    shell/src/ProductIdentity.cpp
)

# The generator writes provenance using the same record type the plugin shell
# parses at runtime, so the two can never drift.
target_include_directories(ZarlinoFactory_Generate PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shell/include
)

target_compile_definitions(ZarlinoFactory_Generate PRIVATE
    ZFACTORY_PROJECT_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shell/project"
    ZFACTORY_SHELL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shell"
)

target_link_libraries(ZarlinoFactory_Generate
    PRIVATE
        zarlino_factory
        juce::juce_gui_basics
        juce::juce_cryptography   # SHA-256 of the canonical specification
)

endif() # ZFACTORY_BUILD_TOOLS
