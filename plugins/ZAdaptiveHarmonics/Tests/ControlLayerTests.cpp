//==============================================================================
/** ZAH-DSP-CTRL-001 — ControlLayer correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/AnalysisCore.h"
#include "../Source/ControlLayer.h"
#include "TestMacros.h"

//==============================================================================
void testControlLayer()
{
    bool _testOk;
    // ZAH-DSP-CTRL-0011 — Boundary parameter values
    TEST("ZAH-DSP-CTRL-0011: Boundary parameter values")
    {
        ControlLayer cl;
        cl.prepare (48000.0);
        ControlVector cv{}; // all zeros (silence)

        // All params at zero
        cl.update (cv, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 48000.0);
        cl.skipSamples (1000);
        auto t = cl.getCurrentTargets();
        CHECK(t.satEven == 0.0f);
        CHECK(t.satOdd == 0.0f);
        CHECK(t.satHarmReduction == 0.0f);
        CHECK(t.tiltAmount == 0.0f);
        CHECK(t.exciterAmount == 0.0f);
        CHECK(t.exciterEnabled == 0.0f);

        // All params at max
        cl.update (cv, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 48000.0);
        cl.skipSamples (1000);
        t = cl.getCurrentTargets();
        CHECK(t.satEven > 0.0f);
        CHECK(t.satOdd > 0.0f);
        CHECK(t.satHarmReduction == 1.0f);
        CHECK(t.tiltAmount > 0.0f);
        CHECK(t.exciterAmount > 0.0f);
        CHECK(t.exciterEnabled == 1.0f);

        // character = -1 = warm
        cl.update (cv, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 48000.0);
        cl.skipSamples (1000);
        t = cl.getCurrentTargets();
        CHECK(t.satEven > 0.0f);
        CHECK(t.satOdd > 0.0f);
        CHECK(t.tiltAmount < 0.0f);
        CHECK(t.exciterAmount < 0.01f);
    }
    END_TEST()

    // ZAH-DSP-CTRL-0012 — Character mapping curve sweep
    TEST("ZAH-DSP-CTRL-0012: Character mapping sweep")
    {
        ControlLayer cl;
        cl.prepare (48000.0);
        ControlVector cv{};

        float prevEven = -999.0f;
        float prevOdd = -999.0f;

        for (int i = 0; i <= 20; ++i)
        {
            const float c = -1.0f + static_cast<float>(i) * 0.1f;
            cl.update (cv, 1.0f, c, 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 48000.0);
            cl.skipSamples (1000);
            auto t = cl.getCurrentTargets();

            if (c < -0.5f)
                CHECK(t.satEven > t.satOdd);
            if (c > 0.5f)
                CHECK(t.satOdd > t.satEven);

            if (prevEven > -900.0f)
                CHECK(t.satEven <= prevEven + 0.01f);
            if (prevOdd > -900.0f)
                CHECK(t.satOdd >= prevOdd - 0.01f);

            prevEven = t.satEven;
            prevOdd = t.satOdd;
        }
    }
    END_TEST()

    // ZAH-DSP-CTRL-0013 — Lift at zero zeroes all targets
    TEST("ZAH-DSP-CTRL-0013: Lift=0 zeroes all targets")
    {
        ControlLayer cl;
        cl.prepare (48000.0);
        ControlVector cv{};

        cl.update (cv, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 48000.0);
        cl.skipSamples (1000);
        auto t = cl.getCurrentTargets();

        CHECK(t.satEven == 0.0f);
        CHECK(t.satOdd == 0.0f);
        CHECK(t.tiltAmount == 0.0f);
        CHECK(t.exciterAmount == 0.0f);
        CHECK(t.satHarmReduction == 1.0f);
        CHECK(t.exciterEnabled == 1.0f);
    }
    END_TEST()

    // ZAH-DSP-CTRL-0014 — Determinism
    TEST("ZAH-DSP-CTRL-0014: Determinism")
    {
        auto run = []() -> ModuleTargets {
            ControlLayer cl;
            cl.prepare (48000.0);
            ControlVector cv{0.5f, 0.3f, 0.4f, 0.6f, 0.7f};
            cl.update (cv, 0.8f, 0.3f, 0.6f, 0.7f, 0.5f, 0.4f, 0.3f, 1.0f, 48000.0);
            cl.skipSamples (1000);
            return cl.getCurrentTargets();
        };

        auto t1 = run();
        for (int i = 0; i < 100; ++i)
        {
            auto t2 = run();
            CHECK(t1.satEven == t2.satEven);
            CHECK(t1.satOdd == t2.satOdd);
            CHECK(t1.satHarmReduction == t2.satHarmReduction);
            CHECK(t1.tiltAmount == t2.tiltAmount);
            CHECK(t1.exciterAmount == t2.exciterAmount);
            CHECK(t1.exciterEnabled == t2.exciterEnabled);
        }
    }
    END_TEST()

    // ZAH-DSP-CTRL-0015 — Adaptive modulation from ControlVector
    TEST("ZAH-DSP-CTRL-0015: Adaptive modulation from analysis")
    {
        ControlLayer cl;
        cl.prepare (48000.0);

        ControlVector noisy{1.0f, 1.0f, 0.0f, 0.0f, 0.5f};
        cl.update (noisy, 1.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 48000.0);
        cl.skipSamples (1000);
        auto tNoisy = cl.getCurrentTargets();

        ControlVector clean{0.0f, 0.0f, 0.0f, 0.0f, 0.5f};
        cl.update (clean, 1.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 48000.0);
        cl.skipSamples (1000);
        auto tClean = cl.getCurrentTargets();

        CHECK(tNoisy.satEven < tClean.satEven);
        CHECK(tNoisy.satOdd < tClean.satOdd);
    }
    END_TEST()
}
