//==============================================================================
/** ZAH-DSP-TILT-001 — Tilt correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/Tilt.h"
#include "TestMacros.h"

//==============================================================================
void testTilt()
{
    bool _testOk;
    // ZAH-DSP-TILT-0011 — Zero tilt passthrough
    TEST("ZAH-DSP-TILT-0011: Zero tilt passthrough")
    {
        Tilt tilt;
        tilt.prepare (48000.0, 512, 2);

        juce::AudioBuffer<float> buffer (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);

        tilt.process (buffer, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = original.getReadPointer (ch);
            const auto* proc = buffer.getReadPointer (ch);
            float maxDiff = 0.0f;
            for (int i = 100; i < 512; ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (proc[i] - orig[i]));
            CHECK(maxDiff < 0.05f);
        }
    }
    END_TEST()

    // ZAH-DSP-TILT-0012 — Polarity sign
    TEST("ZAH-DSP-TILT-0012: Polarity — negative=bass, positive=treble")
    {
        Tilt tilt;
        tilt.prepare (48000.0, 512, 2);

        juce::AudioBuffer<float> lowFreq (2, 512);
        juce::AudioBuffer<float> highFreq (2, 512);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* l = lowFreq.getWritePointer (ch);
            auto* h = highFreq.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
            {
                l[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * i / 48000.0f);
                h[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 8000.0f * i / 48000.0f);
            }
        }

        juce::AudioBuffer<float> lowWarm;  lowWarm.makeCopyOf (lowFreq);
        tilt.process (lowWarm, -6.0f);
        juce::AudioBuffer<float> highWarm; highWarm.makeCopyOf (highFreq);
        tilt.process (highWarm, -6.0f);
        juce::AudioBuffer<float> lowBright; lowBright.makeCopyOf (lowFreq);
        tilt.process (lowBright, 6.0f);
        juce::AudioBuffer<float> highBright; highBright.makeCopyOf (highFreq);
        tilt.process (highBright, 6.0f);

        auto rms = [](const juce::AudioBuffer<float>& b, int ch, int start) {
            float sum = 0.0f;
            for (int i = start; i < b.getNumSamples(); ++i)
                sum += b.getSample (ch, i) * b.getSample (ch, i);
            return std::sqrt (sum / (b.getNumSamples() - start));
        };

        CHECK(rms(lowWarm, 0, 200) > rms(lowBright, 0, 200));
        CHECK(rms(highBright, 0, 200) > rms(highWarm, 0, 200));
    }
    END_TEST()

    // ZAH-DSP-TILT-0013 — Boundary tilt values
    TEST("ZAH-DSP-TILT-0013: Boundary tilt values")
    {
        Tilt tilt;
        tilt.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 500.0f * i / 48000.0f);
        }

        for (float db : { -6.0f, -3.0f, 3.0f, 6.0f, -10.0f, 10.0f })
        {
            juce::AudioBuffer<float> test;
            test.makeCopyOf (buffer);
            tilt.process (test, db);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = test.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                {
                    CHECK(!std::isnan (data[i]));
                    CHECK(!std::isinf (data[i]));
                    CHECK(std::abs (data[i]) < 10.0f);
                }
            }
        }
    }
    END_TEST()

    // ZAH-DSP-TILT-0014 — Silence passthrough
    TEST("ZAH-DSP-TILT-0014: Silence passthrough")
    {
        Tilt tilt;
        tilt.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();

        for (float db : { -6.0f, -3.0f, 0.0f, 3.0f, 6.0f })
        {
            tilt.process (buffer, db);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(data[i] == 0.0f);
            }
        }
    }
    END_TEST()

    // ZAH-DSP-TILT-0015 — Determinism
    TEST("ZAH-DSP-TILT-0015: Determinism")
    {
        Tilt tilt;
        tilt.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> input (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = std::sin (2.0f * juce::MathConstants<float>::pi * (300.0f + ch * 700.0f) * i / 48000.0f);
        }

        juce::AudioBuffer<float> golden;
        golden.makeCopyOf (input);
        tilt.process (golden, 4.0f);

        for (int run = 0; run < 100; ++run)
        {
            Tilt tilt2;
            tilt2.prepare (48000.0, 256, 2);
            juce::AudioBuffer<float> test;
            test.makeCopyOf (input);
            tilt2.process (test, 4.0f);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* g = golden.getReadPointer (ch);
                const auto* t = test.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(t[i] == g[i]);
            }
        }
    }
    END_TEST()
}
