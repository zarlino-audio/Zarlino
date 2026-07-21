/*
    ZTame — DSP Correctness & Fuzz Test Runner
*/
#include <JuceHeader.h>
#include <iostream>

extern void registerCorrectnessTests();
extern void registerFuzzTests();
extern void registerPluginLevelTests();

int main (int, char**)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    registerCorrectnessTests();
    registerFuzzTests();
    registerPluginLevelTests();

    auto& allTests = juce::UnitTest::getAllTests();
    std::cout << "\n=== ZTame DSP Tests: " << allTests.size() << " suites ===" << std::endl;

    juce::UnitTestRunner runner;
    int totalFailures = 0;

    for (auto* test : allTests)
    {
        std::cout << "  " << test->getName() << " ... " << std::flush;

        juce::Array<juce::UnitTest*> single;
        single.add (test);
        runner.runTests (single);

        if (auto* r = runner.getResult (runner.getNumResults() - 1))
        {
            std::cout << r->passes << "/" << (r->passes + r->failures)
                      << " passed";
            if (r->failures > 0)
            {
                std::cout << "  *** " << r->failures << " FAILURES ***";
                totalFailures += r->failures;
            }
            std::cout << std::endl;
        }
    }

    std::cout << "\n=== " << (totalFailures > 0 ? "FAILED" : "ALL PASSED")
              << " ===" << std::endl;
    return totalFailures > 0 ? 1 : 0;
}
