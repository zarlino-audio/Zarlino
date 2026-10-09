//==============================================================================
// ZScorch v2.0 — automation stress: continuous parameter motion must stay
// click-free (no block-boundary envelope steps) and always finite.
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"

using namespace ztest;

namespace
{
    float blockRms (const juce::AudioBuffer<float>& b, int start, int len)
    {
        double acc = 0.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);
            for (int i = start; i < start + len && i < b.getNumSamples(); ++i)
                acc += (double) d[i] * (double) d[i];
        }
        return (float) std::sqrt (acc / juce::jmax (1, len * b.getNumChannels()));
    }
}

class AutomationStressTests : public juce::UnitTest
{
public:
    AutomationStressTests() : juce::UnitTest ("ZScorch Automation Stress") {}

    void runTest() override
    {
        const double sr = 44100.0;
        const int blockSize = 512;

        beginTest ("Lift sweep is smooth at block boundaries");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (sr, blockSize);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "satDriveLow", 0.8f);
            setParam (vts, "exciterEnabled", 1.0f);
            setParam (vts, "exciterAmount", 0.5f);

            juce::MidiBuffer midi;
            float prevTail = -1.0f;
            bool sawSignal = false;

            for (int i = 0; i < 120; ++i)
            {
                setParam (vts, "lift", (float) i / 119.0f);

                juce::AudioBuffer<float> block (2, blockSize);
                fillSine (block, sr, 440.0f, 0.4f);
                proc.processBlock (block, midi);

                expect (allFinite (block), "finite during lift sweep");

                const float head = blockRms (block, 0, 64);

                if (prevTail > 1.0e-5f && head > 1.0e-5f)
                {
                    const float ratio = head / prevTail;
                    expect (ratio > 0.4f && ratio < 2.5f,
                            "no envelope step at block " + juce::String (i)
                            + " (ratio " + juce::String (ratio, 3) + ")");
                    sawSignal = true;
                }

                prevTail = blockRms (block, blockSize - 64, 64);
            }

            expect (sawSignal, "signal was present to measure");
        }

        beginTest ("Full parameter thrash stays finite and bounded");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (sr, blockSize);
            auto& vts = proc.getValueTreeState();

            std::mt19937 rng (4242);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);

            juce::MidiBuffer midi;
            for (int i = 0; i < 300; ++i)
            {
                setParam (vts, "lift", unit (rng));
                setParam (vts, "character", unit (rng) * 2.0f - 1.0f);
                setParam (vts, "mix", unit (rng));
                setParam (vts, "bandCount", (float) juce::roundToInt (unit (rng) * 3.0f));
                setParam (vts, "satDriveLow", unit (rng));
                setParam (vts, "satDriveLoMid", unit (rng));
                setParam (vts, "satDriveHiMid", unit (rng));
                setParam (vts, "satDriveHigh", unit (rng));
                setParam (vts, "satModeLow", unit (rng));
                setParam (vts, "satModeLoMid", unit (rng));
                setParam (vts, "satModeHiMid", unit (rng));
                setParam (vts, "satModeHigh", unit (rng));
                setParam (vts, "tiltAmount", unit (rng));
                setParam (vts, "outputCeiling", -12.0f + unit (rng) * 11.9f);

                juce::AudioBuffer<float> block (2, blockSize);
                fillSine (block, sr, 120.0f + 5000.0f * unit (rng), 0.5f);
                proc.processBlock (block, midi);

                expect (allFinite (block), "finite at thrash block " + juce::String (i));
                expectLessThan (peak (block), 1.3f);
            }
        }

        beginTest ("Ceiling automation tracks without overshoot");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (sr, blockSize);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "lift", 1.0f);
            setParam (vts, "satDriveLow", 1.0f);

            juce::MidiBuffer midi;
            for (int i = 0; i < 60; ++i)
            {
                const float ceilingDb = -0.1f - 11.9f * ((float) i / 59.0f);
                setParam (vts, "outputCeiling", ceilingDb);

                // Process two blocks: the first settles any parameter-update
                // latency, the second is measured against the new ceiling.
                juce::AudioBuffer<float> block (2, blockSize);
                fillSine (block, sr, 220.0f, 0.9f);
                proc.processBlock (block, midi);

                fillSine (block, sr, 220.0f, 0.9f);
                proc.processBlock (block, midi);

                const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
                expectLessThan (peak (block), ceiling + 2.0e-3f,
                                "within ceiling at step " + juce::String (i));
            }
        }

        beginTest ("Wavefold mode under automation does not explode");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (sr, blockSize);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "lift", 1.0f);
            setParam (vts, "satModeLow", 1.0f);      // Wavefold

            juce::MidiBuffer midi;
            for (int i = 0; i < 80; ++i)
            {
                setParam (vts, "satDriveLow", (float) i / 79.0f);

                juce::AudioBuffer<float> block (2, blockSize);
                fillSine (block, sr, 300.0f, 0.7f);
                proc.processBlock (block, midi);

                expect (allFinite (block), "finite wavefold sweep " + juce::String (i));
                expectLessThan (peak (block), 1.3f);
            }
        }
    }
};

static AutomationStressTests automationStressTests;

void registerAutomationStressTests() { juce::ignoreUnused (automationStressTests); }
