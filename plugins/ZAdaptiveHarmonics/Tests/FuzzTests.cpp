//==============================================================================
/** ZAH-FUZZ-001 — Fuzz and adversarial test suite for ZAdaptiveHarmonics. */
//==============================================================================

#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "TestMacros.h"

//==============================================================================
static std::unique_ptr<ZAdaptiveHarmonicsAudioProcessor> makeProcessor(double sr, int bs)
{
    auto p = std::make_unique<ZAdaptiveHarmonicsAudioProcessor>();
    p->setRateAndBufferSizeDetails (sr, bs);
    p->prepareToPlay (sr, bs);
    return p;
}

//==============================================================================
void testFuzz()
{
    bool _testOk;
    // ZAH-FUZZ-0011 — Random parameter combinations: no crash across many iterations
    std::cerr << "\n>>> STARTING FUZZ TESTS <<<" << std::endl;
    TEST("ZAH-FUZZ-0011: Random parameter combinations")
    {
        auto proc = makeProcessor (48000.0, 256);
        auto& vts = proc->getValueTreeState();

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        for (int iter = 0; iter < 5000; ++iter)
        {
            const char* paramIds[] = {
                "lift", "character", "mix",
                "saturatorEven", "saturatorOdd", "saturatorHarmReduction",
                "tiltAmount", "exciterAmount", "exciterEnabled"
            };

            for (auto* id : paramIds)
            {
                auto* p = vts.getRawParameterValue (id);
                if (p)
                    p->store (static_cast<float> (std::rand()) / static_cast<float> (RAND_MAX));
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                const float freq = 50.0f + (std::rand() % 15000);
                const float amp = 0.01f + (std::rand() % 100) * 0.01f;
                for (int i = 0; i < 256; ++i)
                    data[i] = amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * i / 48000.0f);
            }

            proc->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                {
                    CHECK(!std::isnan (data[i]));
                    CHECK(!std::isinf (data[i]));
                }
            }
        }
    }
    END_TEST()

    std::cerr << "\n[fuzz1 done]" << std::endl; // debug
    // ZAH-FUZZ-0012 — NaN injection: output must never contain NaN
    TEST("ZAH-FUZZ-0012: NaN injection -> no NaN output")
    {
        auto proc = makeProcessor (48000.0, 256);
        juce::MidiBuffer midi;

        juce::AudioBuffer<float> warmup (2, 256);
        warmup.clear();
        for (int i = 0; i < 5; ++i)
            proc->processBlock (warmup, midi);

        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos : {0, 64, 128, 255})
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < 256; ++i)
                    data[i] = (i == pos) ? std::numeric_limits<float>::quiet_NaN()
                                         : std::sin (i * 0.1f) * 0.3f;
            }

            proc->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(!std::isnan (data[i]));
            }
        }
    }
    END_TEST()
    std::cerr << "\n[fuzz2 done]" << std::endl; // debug

    // ZAH-FUZZ-0013 — Inf injection: output must never contain Inf
    TEST("ZAH-FUZZ-0013: Inf injection -> no Inf output")
    {
        auto proc = makeProcessor (48000.0, 256);
        juce::MidiBuffer midi;

        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos : {0, 128, 255})
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < 256; ++i)
                    data[i] = (i == pos) ? std::numeric_limits<float>::infinity()
                                         : std::sin (i * 0.1f) * 0.3f;
            }

            proc->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(!std::isinf (data[i]));
            }
        }
    }
    END_TEST()
    std::cerr << "\n[fuzz3 done]" << std::endl; // debug

    // ZAH-FUZZ-0014 — Denormal-heavy input: verify finite output
    TEST("ZAH-FUZZ-0014: Denormal-heavy silence tail")
    {
        auto proc = makeProcessor (48000.0, 256);
        juce::MidiBuffer midi;

        juce::AudioBuffer<float> buffer (2, 256);
        for (int block = 0; block < 50; ++block)
        {
            const float amp = std::pow (10.0f, (-60.0f - static_cast<float> (block) * 3.0f) / 20.0f);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < 256; ++i)
                    data[i] = amp * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f);
            }

            proc->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                    CHECK(!std::isnan (data[i]));
            }
        }
    }
    END_TEST()
    std::cerr << "\n[fuzz4 done]" << std::endl; // debug

    // ZAH-FUZZ-0015 — Rapid automation: thousands of param changes, no crash
    TEST("ZAH-FUZZ-0015: Rapid parameter automation")
    {
        auto proc = makeProcessor (48000.0, 256);
        auto& vts = proc->getValueTreeState();
        juce::MidiBuffer midi;

        juce::AudioBuffer<float> buffer (2, 256);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < 256; ++i)
                data[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
        }

        const char* paramIds[] = {
            "lift", "character", "mix",
            "saturatorEven", "saturatorOdd", "saturatorHarmReduction",
            "tiltAmount", "exciterAmount", "exciterEnabled"
        };

        for (int block = 0; block < 500; ++block)
        {
            const float t = static_cast<float> (block) * 0.1f;
            for (int p = 0; p < 9; ++p)
            {
                auto* param = vts.getRawParameterValue (paramIds[p]);
                if (param)
                {
                    const float val = 0.5f + 0.5f * std::sin (t + p * 0.7f);
                    param->store (val);
                }
            }

            proc->processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);
                for (int i = 0; i < 256; ++i)
                {
                    CHECK(!std::isnan (data[i]));
                    CHECK(!std::isinf (data[i]));
                    CHECK(std::abs (data[i]) < 20.0f);
                }
            }
        }
    }
    END_TEST()
    std::cerr << "\n[fuzz5 done]" << std::endl; // debug
}
