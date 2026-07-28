//==============================================================================
/** ZAH-DSP-SAT-001 — Saturator correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/Saturator.h"
#include "TestMacros.h"

//==============================================================================
void testSaturator()
{
    bool _testOk;
    // ZAH-DSP-SAT-0011 — Bypass: zero even + zero odd = passthrough
    TEST("ZAH-DSP-SAT-0011: Zero drive = passthrough")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);
        const int latency = sat.getLatencySamples();

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);

        sat.process (buffer, 0.0f, 0.0f, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = original.getReadPointer (ch);
            const auto* proc = buffer.getReadPointer (ch);
            for (int i = latency; i < 256; ++i)
            {
                const float diff = std::abs (proc[i] - orig[i - latency]);
                CHECK(diff < 0.01f);
            }
        }
    }
    END_TEST()

    // ZAH-DSP-SAT-0012 — Boundary parameter values
    TEST("ZAH-DSP-SAT-0012: Boundary parameter values")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> bufEven;
        bufEven.makeCopyOf (buffer);
        sat.process (bufEven, 1.5f, 0.0f, 0.0f);

        juce::AudioBuffer<float> bufOdd;
        bufOdd.makeCopyOf (buffer);
        sat.process (bufOdd, 0.0f, 0.78f, 0.0f);

        juce::AudioBuffer<float> bufBoth;
        bufBoth.makeCopyOf (buffer);
        sat.process (bufBoth, 1.5f, 0.78f, 0.0f);

        juce::AudioBuffer<float> bufReduced;
        bufReduced.makeCopyOf (buffer);
        sat.process (bufReduced, 1.5f, 0.78f, 1.0f);

        auto checkFinite = [&_testOk](const juce::AudioBuffer<float>& b) {
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = b.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                {
                    CHECK(!std::isnan (data[i]));
                    CHECK(!std::isinf (data[i]));
                }
            }
        };

        checkFinite (bufEven);
        checkFinite (bufOdd);
        checkFinite (bufBoth);
        checkFinite (bufReduced);
    }
    END_TEST()

    // ZAH-DSP-SAT-0013 — Silence passthrough
    TEST("ZAH-DSP-SAT-0013: Silence passthrough")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();

        for (int trial = 0; trial < 10; ++trial)
        {
            sat.process (buffer, 1.5f, 0.78f, 1.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(data[i] == 0.0f);
            }
        }
    }
    END_TEST()

    // ZAH-DSP-SAT-0014 — Stereo separation
    TEST("ZAH-DSP-SAT-0014: Stereo channel independence")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);
        const int latency = sat.getLatencySamples();

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);
        for (int i = 0; i < 256; ++i)
            left[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);

        sat.process (buffer, 1.0f, 0.5f, 0.5f);

        float leftRms = 0.0f, rightRms = 0.0f;
        for (int i = latency; i < 256; ++i)
        {
            leftRms += left[i] * left[i];
            rightRms += right[i] * right[i];
        }
        const int count = 256 - latency;
        leftRms = std::sqrt (leftRms / count);
        rightRms = std::sqrt (rightRms / count);

        CHECK(leftRms > 0.1f);
        CHECK(rightRms < 0.01f);
    }
    END_TEST()

    // ZAH-DSP-SAT-0015 — Determinism
    TEST("ZAH-DSP-SAT-0015: Determinism — bit-exact repeat")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> input (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = std::sin (2.0f * juce::MathConstants<float>::pi * (440.0f + ch * 110.0f) * i / 48000.0f);
        }

        juce::AudioBuffer<float> golden;
        golden.makeCopyOf (input);
        sat.process (golden, 1.0f, 0.5f, 0.5f);

        for (int run = 0; run < 100; ++run)
        {
            Saturator sat2;
            sat2.prepare (48000.0, 256, 2);
            juce::AudioBuffer<float> test;
            test.makeCopyOf (input);
            sat2.process (test, 1.0f, 0.5f, 0.5f);

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

    // ZAH-DSP-SAT-0016 — Impulse response stability
    TEST("ZAH-DSP-SAT-0016: Impulse response stability")
    {
        Saturator sat;
        sat.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);

        sat.process (buffer, 1.0f, 0.5f, 0.5f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < 256; ++i)
            {
                CHECK(!std::isnan (data[i]));
                CHECK(!std::isinf (data[i]));
                CHECK(std::abs (data[i]) < 10.0f);
            }
        }
    }
    END_TEST()
}
