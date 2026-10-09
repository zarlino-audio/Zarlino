//==============================================================================
// ZScorch v2.0 test runner — DSP correctness, presets, robustness, fuzz.
//==============================================================================
#include <JuceHeader.h>
#include <iostream>

// Suite entry points (each file defines one).
void registerSmokeTests();
void registerSaturatorTests();
void registerExciterTests();
void registerTiltTests();
void registerControlLayerTests();
void registerAnalysisCoreTests();
void registerPluginLevelTests();
void registerRobustnessTests();
void registerAutomationStressTests();
void registerFuzzTests();
void registerLicenseTests();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    registerSmokeTests();
    registerSaturatorTests();
    registerExciterTests();
    registerTiltTests();
    registerControlLayerTests();
    registerAnalysisCoreTests();
    registerPluginLevelTests();
    registerRobustnessTests();
    registerAutomationStressTests();
    registerFuzzTests();
    registerLicenseTests();

    const juce::String filter = (argc > 1) ? juce::String (argv[1]) : juce::String();

    auto& allTests = juce::UnitTest::getAllTests();
    std::cout << "\n=== ZScorch v2.0 test runner: " << allTests.size() << " suites"
              << (filter.isNotEmpty() ? (" (filter: " + filter + ")") : juce::String())
              << " ===" << std::endl;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    int totalFailures = 0;
    for (auto* test : allTests)
    {
        if (filter.isNotEmpty() && ! test->getName().containsIgnoreCase (filter))
            continue;

        std::cout << "  " << test->getName() << " ... " << std::flush;

        juce::Array<juce::UnitTest*> single;
        single.add (test);
        runner.runTests (single);

        // NOTE: UnitTestRunner::runTests() clears its results array, so after the
        // call the results contain exactly this suite's sub-tests.
        int passes = 0, failures = 0;
        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            if (auto* r = runner.getResult (i))
            {
                passes   += r->passes;
                failures += r->failures;
            }
        }

        std::cout << passes << "/" << (passes + failures) << " passed";
        if (failures > 0)
        {
            std::cout << "  *** " << failures << " FAILURES ***";
            totalFailures += failures;
        }
        std::cout << std::endl;

        for (int i = 0; i < runner.getNumResults(); ++i)
            if (auto* r = runner.getResult (i))
                for (const auto& message : r->messages)
                    std::cout << "        !!! " << message << std::endl;
    }

    std::cout << "\n=== " << (totalFailures > 0 ? "FAILED" : "ALL PASSED") << " ===" << std::endl;
    return totalFailures > 0 ? 1 : 0;
}
