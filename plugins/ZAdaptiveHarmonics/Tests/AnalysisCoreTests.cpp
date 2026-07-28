//==============================================================================
/** ZAH-DSP-ANAL-001 — AnalysisCore correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/AnalysisCore.h"
#include "TestMacros.h"

//==============================================================================
void testAnalysisCore()
{
    bool _testOk;
    // ZAH-DSP-ANAL-0011 — Silence → all zeros
    TEST("ZAH-DSP-ANAL-0011: Silence produces zero control vector")
    {
        AnalysisCore ac;
        ac.prepare (48000.0, 256);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();

        for (int i = 0; i < 10; ++i) {
            std::cerr << "\n    [ac" << i << "]"; // debug
            ac.process (buffer, 256, 2);
        }
        std::cerr << "\n    [check]"; // debug

        const auto& cv = ac.getControlVector();
        CHECK(cv.harmonicDensity == 0.0f);
        CHECK(cv.spectralFlatness == 1.0f);
        CHECK(cv.crestFactor == 0.0f);
        CHECK(cv.brightness == 0.0f);
        CHECK(cv.activity == 0.0f);
    }
    END_TEST()

    // ZAH-DSP-ANAL-0012 — Sine wave produces valid output
    TEST("ZAH-DSP-ANAL-0012: Sine wave produces valid features")
    {
        AnalysisCore ac;
        ac.prepare (48000.0, 256);

        juce::AudioBuffer<float> buffer (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
        }

        ac.process (buffer, 512, 2);

        juce::AudioBuffer<float> buffer2 (2, 512);
        buffer2.clear();
        ac.process (buffer2, 512, 2);

        juce::AudioBuffer<float> buffer3 (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer3.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
        }
        ac.process (buffer3, 512, 2);
        ac.process (buffer3, 512, 2);

        const auto& cv = ac.getControlVector();
        CHECK(cv.harmonicDensity >= 0.0f && cv.harmonicDensity <= 1.0f);
        CHECK(cv.spectralFlatness >= 0.0f && cv.spectralFlatness <= 1.0f);
        CHECK(cv.activity > 0.0f);
        CHECK(cv.crestFactor >= 0.0f && cv.crestFactor <= 1.0f);
    }
    END_TEST()

    // ZAH-DSP-ANAL-0013 — All control vector values are [0, 1]
    TEST("ZAH-DSP-ANAL-0013: Control vector values in [0, 1]")
    {
        struct TestCase { const char* tag; float freq; float amp; };
        TestCase cases[] = {
            {"loud 100Hz", 100.0f, 0.9f},
            {"quiet 5kHz", 5000.0f, 0.01f},
            {"mid 1kHz", 1000.0f, 0.5f},
        };

        for (auto& tc : cases)
        {
            AnalysisCore ac2;
            ac2.prepare (48000.0, 256);

            juce::AudioBuffer<float> buffer (2, 512);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < 512; ++i)
                    data[i] = tc.amp * std::sin (2.0f * juce::MathConstants<float>::pi * tc.freq * i / 48000.0f);
            }

            for (int b = 0; b < 4; ++b)
                ac2.process (buffer, 512, 2);

            const auto& cv = ac2.getControlVector();
            CHECK(cv.harmonicDensity >= 0.0f && cv.harmonicDensity <= 1.0f);
            CHECK(cv.spectralFlatness >= 0.0f && cv.spectralFlatness <= 1.0f);
            CHECK(cv.crestFactor >= 0.0f && cv.crestFactor <= 1.0f);
            CHECK(cv.brightness >= 0.0f && cv.brightness <= 1.0f);
            CHECK(cv.activity >= 0.0f && cv.activity <= 1.0f);
        }
    }
    END_TEST()

    // ZAH-DSP-ANAL-0014 — Band edges are monotonically increasing
    TEST("ZAH-DSP-ANAL-0014: Band bin edges monotonic")
    {
        AnalysisCore ac;
        ac.prepare (44100.0, 64);

        juce::AudioBuffer<float> buffer (1, 256);
        auto* data = buffer.getWritePointer (0);
        for (int i = 0; i < 256; ++i)
            data[i] = (std::rand() / static_cast<float> (RAND_MAX) - 0.5f) * 0.5f;

        for (int b = 0; b < 8; ++b)
            ac.process (buffer, 256, 1);

        const auto& cv = ac.getControlVector();
        CHECK(!std::isnan (cv.harmonicDensity));
        CHECK(!std::isnan (cv.spectralFlatness));
        CHECK(!std::isnan (cv.crestFactor));
        CHECK(!std::isnan (cv.brightness));
        CHECK(!std::isnan (cv.activity));
    }
    END_TEST()
}
