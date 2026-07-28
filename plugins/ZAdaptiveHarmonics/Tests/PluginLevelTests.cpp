#include <JuceHeader.h>
#include "PluginProcessor.h"

class ZAdaptiveHarmonicsPluginLevelTests : public juce::UnitTest
{
public:
    ZAdaptiveHarmonicsPluginLevelTests() : juce::UnitTest("ZAdaptiveHarmonics Plugin-Level") {}

    void runTest() override
    {
        beginTest("Bus layout — stereo only");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses  = juce::AudioChannelSet::stereo();
            layout.outputBuses = juce::AudioChannelSet::stereo();
            expect(proc.checkBusesLayoutSupported(layout), "Stereo in/out must be supported");
            
            layout.inputBuses = juce::AudioChannelSet::mono();
            expect(! proc.checkBusesLayoutSupported(layout), "Mono in / stereo out must NOT be supported");
        }

        beginTest("Plugin metadata");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            expect(proc.getName() == "ZAdaptiveHarmonics", "Plugin name");
            expect(! proc.acceptsMidi(), "Must not accept MIDI");
            expect(! proc.producesMidi(), "Must not produce MIDI");
            expect(proc.getTailLengthSeconds() == 0.0, "No tail");
            expect(proc.hasEditor(), "Must have an editor");
        }

        beginTest("Programs — single program");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            expect(proc.getNumPrograms() == 1, "Must have exactly 1 program");
            expect(proc.getCurrentProgram() == 0, "Current program must be 0");
        }

        beginTest("Parameter range validation");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            auto& apvts = proc.getValueTreeState();
            
            expect(apvts.getParameter("lift") != nullptr, "Lift param exists");
            expect(apvts.getParameter("character") != nullptr, "Character param exists");
            expect(apvts.getParameter("mix") != nullptr, "Mix param exists");
            
            auto* lift = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("lift"));
            expect(lift != nullptr, "Lift is a float param");
            expect(std::abs(lift->get() - 0.5f) < 0.01f, "Lift default is 0.5");
        }

        beginTest("Fuzz — NaN/Inf input is handled gracefully");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            proc.prepareToPlay(44100.0, 256);
            
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            
            // Fill with NaN
            const float nan = std::numeric_limits<float>::quiet_NaN();
            for (int i = 0; i < 256; ++i)
                buffer.setSample(0, i, nan);
            buffer.copyFrom(1, 0, buffer, 0, 0, 256);
            
            proc.processBlock(buffer, midi);
            
            // Output should not contain NaN or crash
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    expect(! std::isnan(buffer.getSample(ch, i)),
                           "Output must not contain NaN");
        }

        beginTest("Fuzz — Inf input is handled gracefully");
        {
            ZAdaptiveHarmonicsAudioProcessor proc;
            proc.prepareToPlay(44100.0, 256);
            
            juce::AudioBuffer<float> buffer(2, 256);
            juce::MidiBuffer midi;
            
            const float inf = std::numeric_limits<float>::infinity();
            for (int i = 0; i < 256; ++i)
                buffer.setSample(0, i, inf);
            buffer.copyFrom(1, 0, buffer, 0, 0, 256);
            
            proc.processBlock(buffer, midi);
            
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    expect(! std::isinf(buffer.getSample(ch, i)),
                           "Output must not contain Inf");
        }
    }
};

static ZAdaptiveHarmonicsPluginLevelTests pluginLevelTests;
