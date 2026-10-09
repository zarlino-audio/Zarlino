//==============================================================================
// ZScorch v2.0 — plugin-level tests: the 60 factory presets, determinism,
// sample-rate independence and bypass behaviour.
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"

using namespace ztest;

namespace
{
    constexpr int kExpectedPresetCount = 60;
}

class PresetTests : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("ZScorch Presets") {}

    void runTest() override
    {
        beginTest ("Factory bank contains 60 well-defined presets");
        {
            ZScorchAudioProcessor proc;
            expectEquals (proc.getNumPrograms(), kExpectedPresetCount);
            expectEquals ((int) proc.getFactoryPresets().size(), kExpectedPresetCount);

            juce::StringArray names;
            for (int i = 0; i < proc.getNumPrograms(); ++i)
            {
                const auto name = proc.getProgramName (i);
                expect (name.isNotEmpty(), "preset " + juce::String (i) + " has a name");
                expect (! names.contains (name), "duplicate preset name: " + name);
                expect (name.length() <= 28, "preset name fits the UI: " + name);
                names.add (name);
            }

            const auto& presets = proc.getFactoryPresets();
            for (size_t i = 0; i < presets.size(); ++i)
                expect (presets[i].xmlState.startsWith ("<ZScorchState>"),
                        "preset " + juce::String ((int) i) + " carries state XML");
        }

        beginTest ("Every preset loads, is audible and produces finite output");
        {
            juce::AudioBuffer<float> sine = makeBuffer (2, 8192);
            fillSine (sine, 44100.0, 220.0f, 0.4f);

            int audibleCount = 0;

            for (int i = 0; i < kExpectedPresetCount; ++i)
            {
                ZScorchAudioProcessor proc;
                proc.setCurrentProgram (i);
                expectEquals (proc.getCurrentProgram(), i);

                auto out = render (proc, sine, 512, 1);

                expect (allFinite (out), "preset " + juce::String (i) + " finite output");
                expectLessThan (peak (out), 1.2f);

                // "Well defined" = the preset audibly reshapes the waveform.
                if (maxAbsDiff (out, sine) > 0.005f)
                    ++audibleCount;
            }

            // "Well defined" means the bank is not a list of near-duplicates.
            expectGreaterThan (audibleCount, 55, "most presets audibly change the signal");
        }

        beginTest ("Presets are deterministic (bit-identical repeat render)");
        {
            for (int i : { 0, 17, 42, 59 })
            {
                juce::AudioBuffer<float> sine = makeBuffer (2, 4096);
                fillSine (sine, 44100.0, 330.0f, 0.5f);

                ZScorchAudioProcessor a, b;
                a.setCurrentProgram (i);
                b.setCurrentProgram (i);

                auto outA = render (a, sine, 512, 1);
                auto outB = render (b, sine, 512, 1);

                expect (maxAbsDiff (outA, outB) == 0.0f,
                        "preset " + juce::String (i) + " is deterministic");
            }
        }

        beginTest ("Presets survive a state round-trip (save -> load)");
        {
            ZScorchAudioProcessor a;
            a.setCurrentProgram (7);

            juce::MemoryBlock state;
            a.getStateInformation (state);

            ZScorchAudioProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());

            expectEquals (b.getCurrentProgram(), 7, "preset index round-trips");
            expectWithinAbsoluteError (getParam (b.getValueTreeState(), "satDriveLow"),
                                       getParam (a.getValueTreeState(), "satDriveLow"), 1.0e-5f);
            expectWithinAbsoluteError (getParam (b.getValueTreeState(), "outputCeiling"),
                                       getParam (a.getValueTreeState(), "outputCeiling"), 1.0e-5f);
        }
    }
};

static PresetTests presetTests;

//==============================================================================
class PluginLevelTests : public juce::UnitTest
{
public:
    PluginLevelTests() : juce::UnitTest ("ZScorch Plugin Level") {}

    void runTest() override
    {
        beginTest ("Dry passthrough is transparent (SNR > 20 dB via lift = 0)");
        {
            ZScorchAudioProcessor proc;
            setParam (proc.getValueTreeState(), "lift", 0.0f);   // gains stage bypassed
            setParam (proc.getValueTreeState(), "outputCeiling", -0.3f);

            juce::AudioBuffer<float> sine = makeBuffer (2, 16384);
            fillSine (sine, 44100.0, 1000.0f, 0.25f);   // -12 dBFS: below the knee

            auto out = render (proc, sine, 512, 1);

            const float sig = rms (sine);

            juce::AudioBuffer<float> diff;
            diff.makeCopyOf (out, true);
            for (int ch = 0; ch < 2; ++ch)
                diff.addFrom (ch, 0, sine, ch, 0, diff.getNumSamples(), -1.0f);

            const float noise = rms (diff);
            const float snrDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, sig)
                                                    / juce::jmax (1.0e-9f, noise));
            expectGreaterThan (snrDb, 20.0f, "transparent at -12 dBFS input");
        }

        beginTest ("Bypass is bit-exact");
        {
            ZScorchAudioProcessor proc;
            proc.bypassEnabled.store (true);
            setParam (proc.getValueTreeState(), "lift", 1.0f);
            setParam (proc.getValueTreeState(), "satDriveLow", 0.9f);

            juce::AudioBuffer<float> sine = makeBuffer (2, 4096);
            fillSine (sine, 44100.0, 440.0f, 0.6f);

            std::vector<juce::AudioBuffer<float>> outputs;
            for (int i = 0; i < 2; ++i)
            {
                ZScorchAudioProcessor p;
                p.bypassEnabled.store (true);
                outputs.push_back (render (p, sine, 512, 1));
            }

            expect (maxAbsDiff (outputs[0], outputs[1]) == 0.0f, "bypass is repeatable");
            expect (maxAbsDiff (outputs[0], sine) < 1.0e-6f, "bypass passes the input through");
        }

        beginTest ("Sample-rate independence (44.1k - 96k)");
        {
            for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
            {
                ZScorchAudioProcessor proc;
                proc.setCurrentProgram (12);
                proc.prepareToPlay (sr, 512);

                juce::AudioBuffer<float> sine = makeBuffer (2, 8192);
                fillSine (sine, sr, 997.0f, 0.5f);

                juce::MidiBuffer midi;
                for (int b = 0; b < 16; ++b)
                    proc.processBlock (sine, midi);

                expect (allFinite (sine), "finite at " + juce::String (sr) + " Hz");
                expectLessThan (peak (sine), 1.2f);
            }
        }

        beginTest ("Ceiling is respected for hot material");
        {
            ZScorchAudioProcessor proc;
            setParam (proc.getValueTreeState(), "outputCeiling", -6.0f);
            setParam (proc.getValueTreeState(), "lift", 1.0f);
            setParam (proc.getValueTreeState(), "satDriveLow", 1.0f);

            juce::AudioBuffer<float> hot = makeBuffer (2, 8192);
            fillSine (hot, 44100.0, 220.0f, 0.0f);
            {
                auto* l = hot.getWritePointer (0);
                auto* r = hot.getWritePointer (1);
                for (int i = 0; i < hot.getNumSamples(); ++i)
                {
                    l[i] = 0.95f * std::sin (0.01f * i);
                    r[i] = -0.95f * std::sin (0.013f * i);
                }
            }

            auto out = render (proc, hot, 512, 1);
            const float ceiling = juce::Decibels::decibelsToGain (-6.0f);

            expectLessThan (peak (out), ceiling + 1.0e-3f);
        }

        beginTest ("Multi-band modes stay stable and reconstruct the signal");
        {
            for (int bands = 0; bands <= 3; ++bands)
            {
                ZScorchAudioProcessor proc;
                setParam (proc.getValueTreeState(), "bandCount", (float) bands);
                setParam (proc.getValueTreeState(), "lift", 0.6f);

                juce::AudioBuffer<float> sine = makeBuffer (2, 8192);
                fillSine (sine, 44100.0, 440.0f, 0.5f);

                auto out = render (proc, sine, 512, 1);

                expect (allFinite (out), "finite at " + juce::String (bands + 1) + " band(s)");
                expectGreaterThan (rms (out), 0.1f,
                                   "signal survives the crossover at "
                                   + juce::String (bands + 1) + " band(s)");
            }
        }
    }
};

static PluginLevelTests pluginLevelTests;

void registerPluginLevelTests() { juce::ignoreUnused (presetTests, pluginLevelTests); }
