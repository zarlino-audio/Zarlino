//==============================================================================
/** ZAH-DSP-EXC-001 — Exciter correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/Exciter.h"
#include "TestMacros.h"

//==============================================================================
void testExciter()
{
    bool _testOk;
    // ZAH-DSP-EXC-0011 — True bypass: enabled=0 → bit-exact passthrough
    TEST("ZAH-DSP-EXC-0011: True bypass when disabled")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 5000.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);

        exc.process (buffer, 1.0f, 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = original.getReadPointer (ch);
            const auto* proc = buffer.getReadPointer (ch);
            for (int i = 0; i < 256; ++i)
                CHECK(proc[i] == orig[i]);
        }
    }
    END_TEST()

    // ZAH-DSP-EXC-0012 — Zero amount = passthrough
    TEST("ZAH-DSP-EXC-0012: Zero amount passthrough")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = std::sin (2.0f * juce::MathConstants<float>::pi * 8000.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);

        exc.process (buffer, 0.0f, 1.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = original.getReadPointer (ch);
            const auto* proc = buffer.getReadPointer (ch);
            for (int i = 0; i < 256; ++i)
                CHECK(proc[i] == orig[i]);
        }
    }
    END_TEST()

    // ZAH-DSP-EXC-0013 — Silence passthrough
    TEST("ZAH-DSP-EXC-0013: Silence passthrough")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();

        exc.process (buffer, 1.0f, 1.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < 256; ++i)
                CHECK(data[i] == 0.0f);
        }
    }
    END_TEST()

    // ZAH-DSP-EXC-0014 — Boundary parameters
    TEST("ZAH-DSP-EXC-0014: Boundary parameter values")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 5000.0f * i / 48000.0f);
        }

        for (float amt : { 0.0f, 0.5f, 1.0f })
        {
            juce::AudioBuffer<float> test;
            test.makeCopyOf (buffer);
            exc.process (test, amt, 1.0f);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = test.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                {
                    CHECK(!std::isnan (data[i]));
                    CHECK(!std::isinf (data[i]));
                    CHECK(std::abs (data[i]) < 3.0f);
                }
            }
        }
    }
    END_TEST()

    // ZAH-DSP-EXC-0015 — Determinism
    TEST("ZAH-DSP-EXC-0015: Determinism")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> input (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * 6000.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> golden;
        golden.makeCopyOf (input);
        exc.process (golden, 0.7f, 1.0f);

        for (int run = 0; run < 100; ++run)
        {
            Exciter exc2;
            exc2.prepare (48000.0, 256, 2);
            juce::AudioBuffer<float> test;
            test.makeCopyOf (input);
            exc2.process (test, 0.7f, 1.0f);

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

    // ZAH-DSP-EXC-0016 — Frequency gating: low freq should pass through unaffected
    TEST("ZAH-DSP-EXC-0016: Low freq bypassed by 3.5kHz gate")
    {
        Exciter exc;
        exc.prepare (48000.0, 256, 2);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * i / 48000.0f);
        }

        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);

        exc.process (buffer, 1.0f, 1.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = original.getReadPointer (ch);
            const auto* proc = buffer.getReadPointer (ch);
            float maxDiff = 0.0f;
            for (int i = 0; i < 256; ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (proc[i] - orig[i]));
            CHECK(maxDiff < 0.05f);
        }
    }
    END_TEST()
}
