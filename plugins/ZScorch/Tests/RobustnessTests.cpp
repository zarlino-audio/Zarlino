//==============================================================================
// ZScorch v2.0 — robustness: block sizes, lifecycle, live reconfiguration
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"

using namespace ztest;

class RobustnessTests : public juce::UnitTest
{
public:
    RobustnessTests() : juce::UnitTest ("ZScorch Robustness") {}

    void runTest() override
    {
        beginTest ("All block sizes are handled without artefacts");
        {
            ZScorchAudioProcessor proc;
            proc.setCurrentProgram (5);
            proc.prepareToPlay (44100.0, 2048);

            juce::MidiBuffer midi;
            for (int blockSize : { 1, 7, 32, 64, 128, 512, 1024, 2048 })
            {
                juce::AudioBuffer<float> block (2, blockSize);
                fillSine (block, 44100.0, 440.0f, 0.5f);
                proc.processBlock (block, midi);

                expect (allFinite (block), "finite at block size " + juce::String (blockSize));
                expectLessThan (peak (block), 1.2f);
            }
        }

        beginTest ("Zero-length and single-sample buffers are safe");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> empty (2, 0);
            proc.processBlock (empty, midi);

            juce::AudioBuffer<float> one (2, 1);
            one.clear();
            proc.processBlock (one, midi);
            expect (allFinite (one), "one-sample block handled");
        }

        beginTest ("Prepare / release cycles are repeatable");
        {
            ZScorchAudioProcessor proc;
            for (int cycle = 0; cycle < 10; ++cycle)
            {
                proc.prepareToPlay (48000.0, 256);
                juce::AudioBuffer<float> block (2, 256);
                fillSine (block, 48000.0, 1000.0f, 0.4f);
                juce::MidiBuffer midi;
                proc.processBlock (block, midi);
                expect (allFinite (block), "finite in cycle " + juce::String (cycle));
                proc.releaseResources();
            }
        }

        beginTest ("Band count can be switched live while audio flows");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            setParam (proc.getValueTreeState(), "lift", 0.7f);

            juce::MidiBuffer midi;
            const int order[] = { 0, 3, 1, 2, 0, 2, 3, 1, 0 };

            for (int mode : order)
            {
                proc.getValueTreeState().getParameter ("bandCount")
                    ->setValueNotifyingHost (mode / 3.0f);

                for (int b = 0; b < 4; ++b)
                {
                    juce::AudioBuffer<float> block (2, 512);
                    fillSine (block, 44100.0, 300.0f + 400.0f * (float) b, 0.5f);
                    proc.processBlock (block, midi);
                    expect (allFinite (block), "finite after switching to " + juce::String (mode + 1) + " bands");
                }
                expectEquals (proc.getActiveNumBands(), mode + 1);
            }
        }

        beginTest ("Sample-rate changes are handled");
        {
            ZScorchAudioProcessor proc;
            for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                proc.prepareToPlay (sr, 512);
                juce::AudioBuffer<float> block (2, 512);
                fillSine (block, sr, 997.0f, 0.5f);
                juce::MidiBuffer midi;
                proc.processBlock (block, midi);
                expect (allFinite (block), "finite at " + juce::String (sr) + " Hz");
            }
        }

        beginTest ("Extreme parameter values stay inside bounds");
        {
            ZScorchAudioProcessor proc;
            auto& vts = proc.getValueTreeState();

            setParam (vts, "lift", 1.0f);
            setParam (vts, "character", 1.0f);
            setParam (vts, "mix", 1.0f);          // wet path only: ceiling must bound it
            setParam (vts, "outputCeiling", -12.0f);
            setParam (vts, "exciterEnabled", 1.0f);
            setParam (vts, "exciterAmount", 1.0f);
            setParam (vts, "tiltAmount", 1.0f);
            for (auto* id : { "preEqLowShelfGain", "preEqBellGain", "preEqHighShelfGain",
                              "postEqLowShelfGain", "postEqBellGain", "postEqHighShelfGain" })
                setParam (vts, id, 12.0f);
            for (auto* id : { "bandGainLow", "bandGainLoMid", "bandGainHiMid", "bandGainHigh" })
                setParam (vts, id, 6.0f);
            for (auto* id : { "satDriveLow", "satDriveLoMid", "satDriveHiMid", "satDriveHigh" })
                setParam (vts, id, 1.0f);

            juce::AudioBuffer<float> block (2, 4096);
            fillSine (block, 44100.0, 100.0f, 0.9f);

            auto out = render (proc, block, 512, 1);

            expect (allFinite (out), "finite under extreme settings");
            expectLessThan (peak (out), juce::Decibels::decibelsToGain (-12.0f) + 1.0e-3f,
                            "ceiling respected at -12 dBFS");
        }

        beginTest ("Solo/mute-style gating via band gains does not destabilise");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "bandCount", 3.0f);
            setParam (vts, "lift", 0.8f);

            juce::MidiBuffer midi;
            for (int i = 0; i < 20; ++i)
            {
                setParam (vts, "bandGainLow", (i % 2 == 0) ? -6.0f : 6.0f);
                setParam (vts, "bandGainHigh", (i % 2 == 0) ? 6.0f : -6.0f);

                juce::AudioBuffer<float> block (2, 512);
                fillSine (block, 44100.0, 220.0f, 0.5f);
                proc.processBlock (block, midi);
                expect (allFinite (block), "finite during gain thrash");
            }
        }
    }
};

static RobustnessTests robustnessTests;

void registerRobustnessTests() { juce::ignoreUnused (robustnessTests); }
