#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
    ZPunch — Musical Transient Shaper.

    Independently shapes the attack (punch) and sustain (body) portions of a
    signal using a split-band RMS envelope detector. A tube-style saturation
    stage with 2x oversampling adds optional analog character to the shaped
    transients. A -1 dBFS soft-knee output clipper provides gain-staging safety.

    Signal flow (fixed order):
        1. Input Gain — clean pre-gain, ±18 dB
        2. Envelope Detector — RMS-based, separates attack vs. sustain
        3. Attack Shaper — applies ±24 dB gain to detected transients
        4. Sustain Shaper — applies ±24 dB gain to detected body
        5. Saturation — optional tube-style asymmetric soft-clip, 2x oversampled
        6. Output Clipper — -1 dBFS ceiling, tanh soft-knee
        7. Dry/Wet Mix — parallel blend

    Latency: ~1 ms (from 2x oversampling FIR anti-imaging filter).
    No lookahead — zero added shaper latency.
 */
class ZPunchAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    ZPunchAudioProcessor();
    ~ZPunchAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.005; }

    //==============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Meter values for the UI
    float getInputPeakDb()    const { return inputPeakDb.load(); }
    float getOutputPeakDb()   const { return outputPeakDb.load(); }
    float getAttackAmountDb() const { return attackAmountDb.load(); }
    float getSustainAmountDb()const { return sustainAmountDb.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    // Per-channel envelope detector state
    struct DetectorState
    {
        float rmsSlow    = 0.0f;   // long-term RMS for sustain threshold
        float rmsFast    = 0.0f;   // short-term RMS for transient detection
        float smoothed   = 0.0f;   // smoothed envelope output
        void reset() { rmsSlow = 0.0f; rmsFast = 0.0f; smoothed = 0.0f; }
    };

    //==============================================================================
    // Parameter IDs
    static constexpr const char* paramInputGain   = "inputGain";
    static constexpr const char* paramAttack      = "attack";
    static constexpr const char* paramSustain     = "sustain";
    static constexpr const char* paramAttackTime  = "attackTime";
    static constexpr const char* paramReleaseTime = "releaseTime";
    static constexpr const char* paramSensitivity = "sensitivity";
    static constexpr const char* paramSaturation  = "saturation";
    static constexpr const char* paramOutputGain  = "outputGain";
    static constexpr const char* paramDryWet      = "dryWet";
    static constexpr const char* paramBypass      = "bypass";

    //==============================================================================
    // Core DSP stages (all operate on a single sample, per-channel)
    float processEnvelope (float input, DetectorState& det,
                           float attackTimeMs, float releaseTimeMs,
                           float sensitivity, double sampleRate);

    // Saturation: asymmetric polynomial soft-clip (tube-style, generates 2nd+3rd harmonics)
    static float saturate (float x, float drive);

    // Output safety clipper: tanh-based, -1 dBFS ceiling with soft knee
    static float clipOutput (float x);

    // Helpers
    static float readFloatParam (juce::AudioProcessorValueTreeState& state,
                                 const char* id);
    static bool  readBoolParam  (juce::AudioProcessorValueTreeState& state,
                                 const char* id);
    static float gainFromDb (float db) { return juce::Decibels::decibelsToGain (db); }
    static float dbFromGain (float gain) { return juce::Decibels::gainToDecibels (gain, -120.0f); }

    //==============================================================================
    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    // DSP state
    std::array<DetectorState, 2> detectorStates;    // stereo envelope detectors
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler; // 2x for saturation

    double currentSampleRate  = 44100.0;
    int    maxSamplesPerBlock = 512;

    //==============================================================================
    // Atomic meter values for UI (thread-safe reads)
    std::atomic<float> inputPeakDb     { -120.0f };
    std::atomic<float> outputPeakDb    { -120.0f };
    std::atomic<float> attackAmountDb  { 0.0f };
    std::atomic<float> sustainAmountDb { 0.0f };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZPunchAudioProcessor)
};
