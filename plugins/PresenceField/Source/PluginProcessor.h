#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class PresenceFieldAudioProcessor final : public juce::AudioProcessor
{
public:
    PresenceFieldAudioProcessor();
    ~PresenceFieldAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.8; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    float getInputRmsDb() const { return inputRmsDb.load(); }
    float getDistanceFactor() const { return distanceFactor.load(); }
    float getAirFrequencyHz() const { return airFrequencyHz.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct DelayLine
    {
        void prepare (int maxDelaySamples);
        float read (int delaySamples) const;
        void write (float value);

        std::vector<float> buffer;
        int writeIndex = 0;
    };

    void updateFilters();
    static float gainFromDb (float db) { return juce::Decibels::decibelsToGain (db); }
    static float softClip (float x, float drive);
    static void applyWidth (float& left, float& right, float width);

    juce::AudioProcessorValueTreeState parameters;
    juce::dsp::IIR::Filter<float> presencePeak[2];
    juce::dsp::IIR::Filter<float> airPeak[2];
    juce::dsp::IIR::Filter<float> memoryWarmth[2];
    juce::dsp::IIR::Filter<float> memoryDark[2];
    std::array<DelayLine, 2> anchorDelay;
    std::array<DelayLine, 2> memoryDelay;

    double currentSampleRate = 44100.0;
    int samplesUntilFilterUpdate = 0;
    float smoothedDistance = 0.0f;
    float lfoPhase = 0.0f;

    std::atomic<float> inputRmsDb { -100.0f };
    std::atomic<float> distanceFactor { 0.0f };
    std::atomic<float> airFrequencyHz { 12000.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresenceFieldAudioProcessor)
};
