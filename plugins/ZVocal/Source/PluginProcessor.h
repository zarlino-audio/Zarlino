#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

class ZVocalAudioProcessor final : public juce::AudioProcessor
{
public:
    ZVocalAudioProcessor();
    ~ZVocalAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    float getStageBReductionDb() const { return stageBReductionDb.load(); }
    float getStageDReductionDb() const { return stageDReductionDb.load(); }
    float getStageFReductionDb() const { return stageFReductionDb.load(); }
    float getInputPeakDb() const { return inputPeakDb.load(); }
    float getOutputPeakDb() const { return outputPeakDb.load(); }
    int getReportedLatencySamples() const { return getLatencySamples(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    struct Envelope
    {
        void reset() { value = 0.0f; }
        float process (float input, float attackMs, float releaseMs, double sampleRate);
        float value = 0.0f;
    };

    struct StereoFilter
    {
        void prepare (double sampleRate, int maxBlockSize);
        void reset();

        // Writes new coefficient VALUES into the pre-allocated, shared coefficient
        // object. Must only be called after prepare(), which allocates the storage
        // off the audio thread; the in-place update itself does not allocate.
        void setValues (const std::array<float, 6>& values);

        float processSample (int channel, float value);

        std::array<juce::dsp::IIR::Filter<float>, 2> filters;
    };

    struct ChannelState
    {
        Envelope fet;
        Envelope optoFast;
        Envelope optoSlow;
        Envelope deess;
        float optoReleaseMs = 800.0f;
    };

    void updateFilters();
    void updateLatency();
    void processStageA (juce::AudioBuffer<float>& buffer);
    void processStageB (juce::AudioBuffer<float>& buffer);
    void processStageC (juce::AudioBuffer<float>& buffer);
    void processStageD (juce::AudioBuffer<float>& buffer);
    void processStageE (juce::AudioBuffer<float>& buffer);
    void processStageF (juce::AudioBuffer<float>& buffer);
    void processLimiter (juce::AudioBuffer<float>& buffer);
    void sanitizeBuffer (juce::AudioBuffer<float>& buffer);

    float readFloat (const char* id) const;
    int readChoice (const char* id) const;
    bool readBool (const char* id) const;

    static float gainFromDb (float db) { return juce::Decibels::decibelsToGain (db); }
    static float dbFromGain (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }
    static float softClip (float x, float amount);
    static float compressSample (float input, Envelope& detector, float thresholdDb, float ratio,
                                 float attackMs, float releaseMs, float rangeDb, double sampleRate,
                                 float* reductionDb = nullptr);

    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;
    int maxSamplesPerBlock = 512;
    int filterUpdateCountdown = 0;

    StereoFilter hpf;
    StereoFilter cSubHpf;
    StereoFilter lowMud;
    StereoFilter mood;
    StereoFilter presence;
    StereoFilter airShelf;
    StereoFilter cLowShelf;
    StereoFilter cBox;
    StereoFilter cPresence;
    StereoFilter cSibilance;
    StereoFilter eMidBand;
    StereoFilter eHighPass;
    StereoFilter fSidechain;
    StereoFilter fLowPass;
    StereoFilter fHighPass;

    std::array<ChannelState, 2> channelStates;

    std::atomic<float> stageBReductionDb { 0.0f };
    std::atomic<float> stageDReductionDb { 0.0f };
    std::atomic<float> stageFReductionDb { 0.0f };
    std::atomic<float> inputPeakDb { -120.0f };
    std::atomic<float> outputPeakDb { -120.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZVocalAudioProcessor)
};
