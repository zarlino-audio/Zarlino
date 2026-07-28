//==============================================================================
/** Test runner entry point. */
//==============================================================================

#include <JuceHeader.h>
#include "TestMacros.h"

//==============================================================================
// Test declarations
void testSmoke();
void testControlLayer();
void testSaturator();
void testTilt();
void testExciter();
void testAnalysisCore();
void testFuzz();
void testPluginLevel();

//==============================================================================
int gPassed = 0;
int gFailed = 0;

int main()
{
    std::cerr << "\n=== ZAdaptiveHarmonics DSP Tests ===" << std::endl;
    juce::ScopedJuceInitialiser_GUI init;

    testSmoke();
    testControlLayer();
    testSaturator();
    testTilt();
    testExciter();
    testAnalysisCore();
    testFuzz();
    testPluginLevel();

    std::cerr << "\n=== " << gPassed << " passed, " << gFailed << " failed ===" << std::endl;
    return gFailed > 0 ? 1 : 0;
}
