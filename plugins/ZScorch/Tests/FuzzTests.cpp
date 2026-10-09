//==============================================================================
// ZScorch v2.0 — fuzz tests: hostile inputs and hostile control values.
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"
#include "Saturator.h"
#include "Exciter.h"
#include "Tilt.h"

using namespace ztest;

namespace
{
    void fillHostile (juce::AudioBuffer<float>& buf, int kind, std::mt19937& rng)
    {
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                switch (kind)
                {
                    case 0: d[i] = std::numeric_limits<float>::quiet_NaN(); break;
                    case 1: d[i] = std::numeric_limits<float>::infinity(); break;
                    case 2: d[i] = -std::numeric_limits<float>::infinity(); break;
                    case 3: d[i] = (i % 2 == 0) ? 1.0e6f : -1.0e6f; break;
                    case 4: d[i] = 1.0e-30f * (unit (rng) - 0.5f); break;   // denormals
                    case 5: d[i] = std::numeric_limits<float>::max() * (i % 3 == 0 ? 1.0f : -1.0f); break;
                    default: d[i] = 4.0f * (unit (rng) - 0.5f); break;
                }
            }
        }
    }
}

class FuzzTests : public juce::UnitTest
{
public:
    FuzzTests() : juce::UnitTest ("ZScorch Fuzz") {}

    void runTest() override
    {
        std::mt19937 rng (20260214);

        beginTest ("Hostile inputs are sanitised to finite, bounded output");
        {
            for (int kind = 0; kind <= 6; ++kind)
            {
                ZScorchAudioProcessor proc;
                proc.prepareToPlay (44100.0, 512);
                proc.setCurrentProgram (20);

                juce::MidiBuffer midi;
                for (int b = 0; b < 8; ++b)
                {
                    juce::AudioBuffer<float> block (2, 512);
                    fillHostile (block, kind, rng);
                    proc.processBlock (block, midi);

                    expect (allFinite (block),
                            "kind " + juce::String (kind) + " block " + juce::String (b));
                    expectLessThan (peak (block), 1.5f);
                }
            }
        }

        beginTest ("Random NaN/Inf sprinkles inside musical material");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (48000.0, 256);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "lift", 0.9f);

            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            juce::MidiBuffer midi;

            for (int b = 0; b < 40; ++b)
            {
                juce::AudioBuffer<float> block (2, 256);
                fillSine (block, 48000.0, 440.0f, 0.5f);

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = block.getWritePointer (ch);
                    for (int k = 0; k < 4; ++k)
                    {
                        const int pos = (int) (unit (rng) * (block.getNumSamples() - 1));
                        d[pos] = (unit (rng) < 0.5f) ? std::numeric_limits<float>::quiet_NaN()
                                                     : std::numeric_limits<float>::infinity();
                    }
                }

                proc.processBlock (block, midi);
                expect (allFinite (block), "finite after NaN sprinkle " + juce::String (b));
            }
        }

        beginTest ("Modules survive hostile input directly");
        {
            Saturator sat;
            sat.prepare (44100.0, 512, 2);

            Exciter ex;
            ex.prepare (44100.0, 512, 2);

            Tilt tilt;
            tilt.prepare (44100.0, 512, 2);

            for (int kind = 0; kind <= 6; ++kind)
            {
                juce::AudioBuffer<float> a = makeBuffer (2, 1024);
                fillHostile (a, kind, rng);
                sat.process (a, 0.8f, SaturationMode::Germanium, 0.7f);
                expect (allFinite (a), "saturator kind " + juce::String (kind));

                juce::AudioBuffer<float> b = makeBuffer (2, 1024);
                fillHostile (b, kind, rng);
                ex.process (b, 0.7f, 1.0f);
                expect (allFinite (b), "exciter kind " + juce::String (kind));

                juce::AudioBuffer<float> c = makeBuffer (2, 1024);
                fillHostile (c, kind, rng);
                tilt.process (c, 4.0f);
                expect (allFinite (c), "tilt kind " + juce::String (kind));
            }
        }

        beginTest ("Random parameter soup with random audio stays stable");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            auto& vts = proc.getValueTreeState();

            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            juce::MidiBuffer midi;

            for (int b = 0; b < 200; ++b)
            {
                setParam (vts, "lift", unit (rng));
                setParam (vts, "character", 2.0f * unit (rng) - 1.0f);
                setParam (vts, "bandCount", (float) juce::roundToInt (3.0f * unit (rng)));
                setParam (vts, "exciterEnabled", unit (rng) < 0.5f ? 0.0f : 1.0f);
                setParam (vts, "exciterAmount", unit (rng));
                setParam (vts, "oversample4x", unit (rng) < 0.2f ? 1.0f : 0.0f);

                juce::AudioBuffer<float> block (2, 512);
                fillHostile (block, (int) (unit (rng) * 7.0f), rng);
                proc.processBlock (block, midi);

                expect (allFinite (block), "soup block " + juce::String (b));
                expectLessThan (peak (block), 1.5f);
            }
        }

        beginTest ("Repeated HQ toggling mid-stream is safe");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);
            auto& vts = proc.getValueTreeState();
            setParam (vts, "lift", 0.8f);

            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b)
            {
                setParam (vts, "oversample4x", (b % 2 == 0) ? 1.0f : 0.0f);

                juce::AudioBuffer<float> block (2, 512);
                fillSine (block, 44100.0, 1000.0f, 0.5f);
                proc.processBlock (block, midi);

                expect (allFinite (block), "HQ toggle block " + juce::String (b));
                expectLessThan (peak (block), 1.2f);
            }
        }
    }
};

static FuzzTests fuzzTests;

void registerFuzzTests() { juce::ignoreUnused (fuzzTests); }
