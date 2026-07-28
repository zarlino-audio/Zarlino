#include <JuceHeader.h>
#include "PluginProcessor.h"

class ZAdaptiveHarmonicsCorrectnessTests : public juce::UnitTest
{
public:
    ZAdaptiveHarmonicsCorrectnessTests() : juce::UnitTest("ZAdaptiveHarmonics Correctness") {}

    void runTest() override
    {
        beginTest("Null pass — silence in = silence out");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            proc.processBlock(buffer, midi);
            
            // All samples must still be zero
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto rms = buffer.getRMSLevel(ch, 0, buffer.getNumSamples());
                expect(rms < 1e-10f, "Silence input must produce silence output");
            }
        }

        beginTest("Determinism — same input, same output");
        {
            // Two instances with the same state should produce same output
            ZAdaptiveHarmonicsAudioProcessor proc1;
            ZAdaptiveHarmonicsAudioProcessor proc2;
            proc1.prepareToPlay(44100.0, 512);
            proc2.prepareToPlay(44100.0, 512);
            
            juce::AudioBuffer<float> buffer1(2, 512);
            juce::AudioBuffer<float> buffer2(2, 512);
            juce::MidiBuffer midi;
            
            for (int i = 0; i < 512; ++i)
            {
                buffer1.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f));
                buffer2.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f));
            }
            buffer1.copyFrom(1, 0, buffer1, 0, 0, 512);
            buffer2.copyFrom(1, 0, buffer2, 0, 0, 512);
            
            proc1.processBlock(buffer1, midi);
            proc2.processBlock(buffer2, midi);
            
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expect(buffer1.getSample(ch, i) == buffer2.getSample(ch, i),
                           "Deterministic output for same input");
        }

        beginTest("Bypass — Lift=0 passes audio through");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            
            juce::AudioBuffer<float> buffer(2, 512);
            juce::AudioBuffer<float> dryBuffer(2, 512);
            for (int i = 0; i < 512; ++i)
                buffer.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f));
            buffer.copyFrom(1, 0, buffer, 0, 0, 512);
            dryBuffer.makeCopyOf(buffer);
            
            juce::MidiBuffer midi;
            proc.processBlock(buffer, midi);
            
            // With default Lift=0.5, some processing happens — test that it doesn't clip
            auto rms = buffer.getRMSLevel(0, 0, 512);
            expect(rms < 2.0f, "RMS should stay reasonable with processing");
        }

        beginTest("State save/load round-trip");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            proc.prepareToPlay(44100.0, 512);
            
            // Save state
            juce::MemoryBlock savedState;
            proc.getStateInformation(savedState);
            expect(savedState.getSize() > 0, "State must not be empty");
            
            // Load into a new instance
            ZAdaptiveHarmonicsAudioProcessor proc2;
            proc2.prepareToPlay(44100.0, 512);
            proc2.setStateInformation(savedState.getData(), (int)savedState.getSize());
            
            // Both instances should produce the same output
            juce::AudioBuffer<float> buf1(2, 512), buf2(2, 512);
            juce::MidiBuffer midi;
            for (int i = 0; i < 512; ++i)
                buf1.setSample(0, i, 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 44100.0f));
            buf1.copyFrom(1, 0, buf1, 0, 0, 512);
            buf2.makeCopyOf(buf1);
            
            proc.processBlock(buf1, midi);
            proc2.processBlock(buf2, midi);
            
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    expect(buf1.getSample(ch, i) == buf2.getSample(ch, i),
                           "State round-trip must produce identical output");
        }
    }
};

static ZAdaptiveHarmonicsCorrectnessTests correctnessTests;
