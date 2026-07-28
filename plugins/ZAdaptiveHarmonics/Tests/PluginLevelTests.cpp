//==============================================================================
/** ZAH-PLUG — Plugin-level correctness tests. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "TestMacros.h"

//==============================================================================
static void setParams (juce::AudioProcessorValueTreeState& vts,
                       float lift, float character, float mix,
                       float satEven, float satOdd, float satHarmRed,
                       float tilt, float excAmt, float excEn)
{
    struct ParamPair { const char* id; float val; };
    ParamPair pairs[] = {
        {"lift", lift}, {"character", character}, {"mix", mix},
        {"saturatorEven", satEven}, {"saturatorOdd", satOdd},
        {"saturatorHarmReduction", satHarmRed},
        {"tiltAmount", tilt}, {"exciterAmount", excAmt}, {"exciterEnabled", excEn}
    };
    for (auto& pp : pairs)
    {
        auto* p = vts.getRawParameterValue (pp.id);
        if (p)
            p->store (pp.val);
    }
}

static std::unique_ptr<ZAdaptiveHarmonicsAudioProcessor> makeProcessor(double sr, int bs)
{
    auto p = std::make_unique<ZAdaptiveHarmonicsAudioProcessor>();
    p->setRateAndBufferSizeDetails (sr, bs);
    p->prepareToPlay (sr, bs);
    return p;
}

//==============================================================================
void testPluginLevel()
{
    bool _testOk;
    // ZAH-PLUG-0011 — Determinism: 100 runs, bit-exact
    TEST("ZAH-PLUG-0011: Determinism — 100 runs bit-exact")
    {
        juce::AudioBuffer<float> input (2, 512);
        juce::Random fixedRng (42);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = fixedRng.nextFloat() * 0.5f - 0.25f;
        }
        juce::MidiBuffer midi;

        auto golden = makeProcessor (48000.0, 512);
        auto& gvts = golden->getValueTreeState();
        setParams (gvts, 0.7f, 0.3f, 0.9f, 0.6f, 0.5f, 0.8f, 0.4f, 0.3f, 0.0f);

        juce::AudioBuffer<float> goldenOut;
        goldenOut.makeCopyOf (input);

        juce::AudioBuffer<float> settleWarmup (2, 512);
        settleWarmup.makeCopyOf (input);
        for (int i = 0; i < 5; ++i)
        {
            golden->processBlock (settleWarmup, midi);
            settleWarmup.makeCopyOf (input);
        }

        golden->processBlock (goldenOut, midi);

        for (int run = 0; run < 100; ++run)
        {
            auto test = makeProcessor (48000.0, 512);
            auto& tvts = test->getValueTreeState();
            setParams (tvts, 0.7f, 0.3f, 0.9f, 0.6f, 0.5f, 0.8f, 0.4f, 0.3f, 0.0f);

            juce::AudioBuffer<float> testOut;
            testOut.makeCopyOf (input);

            juce::AudioBuffer<float> tw (2, 512);
            tw.makeCopyOf (input);
            for (int i = 0; i < 5; ++i)
            {
                test->processBlock (tw, midi);
                tw.makeCopyOf (input);
            }

            test->processBlock (testOut, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* g = goldenOut.getReadPointer (ch);
                const auto* t = testOut.getReadPointer (ch);
                for (int i = 0; i < 512; ++i)
                    CHECK(t[i] == g[i]);
            }
        }
    }
    END_TEST()

    // ZAH-PLUG-0012 — Lift parameter sweep
    TEST("ZAH-PLUG-0012: Lift sweep — output grows monotonically")
    {
        juce::AudioBuffer<float> input (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
        }
        juce::MidiBuffer midi;

        float prevRms = -1.0f;
        for (int step = 0; step <= 10; ++step)
        {
            const float lift = step * 0.1f;

            auto proc = makeProcessor (48000.0, 512);
            auto& vts = proc->getValueTreeState();
            setParams (vts, lift, 0.0f, 1.0f, 0.7f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f);

            juce::AudioBuffer<float> out;
            out.makeCopyOf (input);
            proc->processBlock (out, midi);

            float rms = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = out.getReadPointer (ch);
                for (int i = 0; i < 512; ++i)
                    rms += data[i] * data[i];
            }
            rms = std::sqrt (rms / (2.0f * 512.0f));

            if (prevRms >= 0.0f)
                CHECK(rms >= prevRms * 0.99f);

            prevRms = rms;
        }
    }
    END_TEST()

    // ZAH-PLUG-0013 — Character sweep: warm vs bright
    TEST("ZAH-PLUG-0013: Character sweep — warm vs bright")
    {
        juce::AudioBuffer<float> input (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 500.0f * i / 48000.0f);
        }
        juce::MidiBuffer midi;

        auto warm = makeProcessor (48000.0, 512);
        setParams (warm->getValueTreeState(), 1.0f, -1.0f, 1.0f, 0.7f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f);
        juce::AudioBuffer<float> warmOut;
        warmOut.makeCopyOf (input);
        warm->processBlock (warmOut, midi);

        auto bright = makeProcessor (48000.0, 512);
        setParams (bright->getValueTreeState(), 1.0f, 1.0f, 1.0f, 0.0f, 0.7f, 0.5f, 1.0f, 1.0f, 1.0f);
        juce::AudioBuffer<float> brightOut;
        brightOut.makeCopyOf (input);
        bright->processBlock (brightOut, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* w = warmOut.getReadPointer (ch);
            const auto* b = brightOut.getReadPointer (ch);
            for (int i = 0; i < 512; ++i)
            {
                CHECK(!std::isnan (w[i]) && !std::isinf (w[i]));
                CHECK(!std::isnan (b[i]) && !std::isinf (b[i]));
            }
        }

        float diffSum = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* w = warmOut.getReadPointer (ch);
            const auto* b = brightOut.getReadPointer (ch);
            for (int i = 0; i < 512; ++i)
                diffSum += std::abs (w[i] - b[i]);
        }
        CHECK(diffSum > 0.01f);
    }
    END_TEST()

    // ZAH-PLUG-0014 — Mix parameter sweep: 0=dry, 1=full wet
    TEST("ZAH-PLUG-0014: Mix sweep — dry at 0, full at 1")
    {
        juce::AudioBuffer<float> input (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = input.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                data[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 800.0f * i / 48000.0f);
        }
        juce::MidiBuffer midi;

        auto dry = makeProcessor (48000.0, 512);
        setParams (dry->getValueTreeState(), 1.0f, 0.0f, 0.0f, 0.7f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f);
        juce::AudioBuffer<float> dryOut;
        dryOut.makeCopyOf (input);
        dry->processBlock (dryOut, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = input.getReadPointer (ch);
            const auto* proc = dryOut.getReadPointer (ch);
            for (int i = 0; i < 512; ++i)
                CHECK(proc[i] == orig[i]);
        }

        auto wet = makeProcessor (48000.0, 512);
        setParams (wet->getValueTreeState(), 1.0f, 0.0f, 1.0f, 0.7f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f);
        juce::AudioBuffer<float> wetOut;
        wetOut.makeCopyOf (input);
        wet->processBlock (wetOut, midi);

        float diffSum = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* orig = input.getReadPointer (ch);
            const auto* proc = wetOut.getReadPointer (ch);
            for (int i = 0; i < 512; ++i)
                diffSum += std::abs (proc[i] - orig[i]);
        }
        CHECK(diffSum > 0.1f);
    }
    END_TEST()
}
