//==============================================================================
// Zarlino Plugin Factory - test runner.
//
// Mirrors the ZScorch / ZTame test runners: every suite is a juce::UnitTest
// registered at static-init time, and this runner prints per-suite pass counts
// and the failure text, then exits non-zero if anything failed.
//==============================================================================

#include <JuceHeader.h>

#include <iostream>

void registerSpecValidationTests();
void registerRegistryTests();
void registerGraphConstructionTests();
void registerParameterMacroTests();
void registerAudioProcessingTests();
void registerDeterminismTests();
void registerSerializationTests();
void registerRealtimeSafetyTests();
void registerExampleSpecTests();
void registerUiPlanTests();

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    registerSpecValidationTests();
    registerRegistryTests();
    registerGraphConstructionTests();
    registerParameterMacroTests();
    registerAudioProcessingTests();
    registerDeterminismTests();
    registerSerializationTests();
    registerRealtimeSafetyTests();
    registerExampleSpecTests();
    registerUiPlanTests();

    const juce::String filter = (argc > 1) ? juce::String (argv[1]) : juce::String();

    auto& allTests = juce::UnitTest::getAllTests();

    std::cout << "\n=== Zarlino Plugin Factory test runner: " << allTests.size() << " suites"
              << (filter.isNotEmpty() ? (" (filter: " + filter + ")") : juce::String())
              << " ===" << std::endl;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    int totalFailures = 0;
    int totalPasses = 0;
    int suitesRun = 0;

    for (auto* test : allTests)
    {
        if (filter.isNotEmpty() && ! test->getName().containsIgnoreCase (filter))
            continue;

        ++suitesRun;
        std::cout << "  " << test->getName() << " ... " << std::flush;

        juce::Array<juce::UnitTest*> single;
        single.add (test);
        runner.runTests (single);

        int passes = 0, failures = 0;

        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            if (auto* result = runner.getResult (i))
            {
                passes += result->passes;
                failures += result->failures;
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
        {
            if (auto* result = runner.getResult (i))
            {
                for (const auto& message : result->messages)
                    std::cout << "      " << message.toStdString() << std::endl;
            }
        }

        totalPasses += passes;
    }

    std::cout << "=== " << suitesRun << " suites, " << totalPasses << " assertions passed, "
              << totalFailures << " failed ===" << std::endl;

    if (totalFailures == 0 && suitesRun > 0)
        std::cout << "=== ALL PASSED ===" << std::endl;

    return totalFailures == 0 ? 0 : 1;
}
