/*
    ZVocal - DSP correctness & fuzz test runner.

    Run the built executable directly (no ctest registration, same as ZTame_Tests):
        build/plugins/ZVocal/ZVocal_Tests_artefacts/Release/ZVocal_Tests.exe

    It prints one line per suite and returns non-zero if any expectation failed.

    NOTE: processors created by the test files are intentionally leaked.
    AudioProcessor teardown can hang console builds (APVTS / message-manager
    ordering) - same convention as the other Zarlino plugin suites.
*/

#include <JuceHeader.h>
#include <iostream>

extern void registerCorrectnessTests();
extern void registerFuzzTests();

int main (int, char**)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    registerCorrectnessTests();
    registerFuzzTests();

    auto& allTests = juce::UnitTest::getAllTests();
    std::cout << "\n=== ZVocal DSP Tests: " << allTests.size() << " suites ===" << std::endl;

    juce::UnitTestRunner runner;
    int totalFailures = 0;

    for (auto* test : allTests)
    {
        std::cout << "  " << test->getName() << " ... " << std::flush;

        juce::Array<juce::UnitTest*> single;
        single.add (test);
        runner.runTests (single);

        // runTests() clears the result list at the start of each call, so after it
        // returns the list holds ONLY this suite's results - but a suite may call
        // beginTest() several times, each appending a new result. Reading just the
        // last one would hide failures from earlier blocks, so aggregate them all.
        int suitePasses = 0, suiteFailures = 0;
        juce::StringArray suiteMessages;

        for (int i = 0; i < runner.getNumResults(); ++i)
        {
            if (auto* r = runner.getResult (i))
            {
                suitePasses += r->passes;
                suiteFailures += r->failures;
                suiteMessages.addArray (r->messages);
            }
        }

        std::cout << suitePasses << "/" << (suitePasses + suiteFailures) << " passed";

        if (suiteFailures > 0)
        {
            std::cout << "  *** " << suiteFailures << " FAILURES ***";
            totalFailures += suiteFailures;

            std::cout << std::endl;
            for (const auto& message : suiteMessages)
                std::cout << "      " << message << std::endl;
        }

        std::cout << std::endl;
    }

    std::cout << "\n=== " << (totalFailures > 0 ? "FAILED" : "ALL PASSED") << " ===" << std::endl;
    return totalFailures > 0 ? 1 : 0;
}
