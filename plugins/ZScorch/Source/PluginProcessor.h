#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include <memory>

//==============================================================================
/**
    ZScorch — Multi-mode, multi-band harmonic saturator (Flagship Series #8).

    Signal chain (fixed order):
        1. Input Gain (±24 dB)
        2. Pre-Emphasis EQ (Low Shelf + Parametric Bell + High Shelf, RBJ biquad)
        3. LR4 Crossover Split (4 bands: Low, Lo-Mid, Hi-Mid, High)
        4. Saturation Engine × 4 bands (6 modes: Tube/Tape/Transistor/Germanium/Diode/Wavefold)
           Forced 4× oversampling via 65-tap polyphase FIR
        5. LR4 Crossover Recombine
        6. Post-Emphasis EQ (same topology as Pre-EQ)
        7. Output Gain + Ceiling Limiter (1ms lookahead, arctan soft-clip)
        8. Dry/Wet Mix (dry path delay-compensated)

    Total: 52 automatable parameters. Latency: ~132 samples at 48 kHz.
 */
class ZScorchAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    ZScorchAudioProcessor();
    ~ZScorchAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

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

    // Meters for the UI
    float getInputLevelDb() const    { return inputLevelDb.load(); }
    float getOutputLevelDb() const   { return outputLevelDb.load(); }
    float getGainReductionDb() const { return gainReductionDb.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    enum SaturationMode
    {
        Tube        = 0,
        Tape        = 1,
        Transistor  = 2,
        Germanium   = 3,
        Diode       = 4,
        Wavefold    = 5,
        NumModes
    };

    static constexpr int numBands = 4;  // Low, Lo-Mid, Hi-Mid, High

private:
    //==============================================================================
    // Parameter IDs — Input
    static constexpr const char* paramInputGain = "inputGain";

    // Pre-EQ Low Shelf
    static constexpr const char* paramPreEqLowFreq  = "preEqLowFreq";
    static constexpr const char* paramPreEqLowGain  = "preEqLowGain";
    static constexpr const char* paramPreEqLowQ     = "preEqLowQ";
    // Pre-EQ Bell
    static constexpr const char* paramPreEqBellFreq = "preEqBellFreq";
    static constexpr const char* paramPreEqBellGain = "preEqBellGain";
    static constexpr const char* paramPreEqBellQ    = "preEqBellQ";
    // Pre-EQ High Shelf
    static constexpr const char* paramPreEqHighFreq = "preEqHighFreq";
    static constexpr const char* paramPreEqHighGain = "preEqHighGain";
    static constexpr const char* paramPreEqHighQ    = "preEqHighQ";

    // Crossover frequencies
    static constexpr const char* paramXOverLowMid     = "xOverLowMid";
    static constexpr const char* paramXOverMidHiMid   = "xOverMidHiMid";
    static constexpr const char* paramXOverHiMidHigh  = "xOverHiMidHigh";

    // Saturation — per-band parameters
    static const char* satParamId (const char* base, int band);

    static constexpr const char* satModeBase      = "satMode";
    static constexpr const char* satDriveBase     = "satDrive";
    static constexpr const char* satAsymBase      = "satAsym";
    static constexpr const char* satMixBase       = "satMix";
    static constexpr const char* satSoloBase      = "satSolo";
    static constexpr const char* satMuteBase      = "satMute";

    static constexpr const char* bandNames[4] = { "Lo", "LoMid", "HiMid", "Hi" };

    // Post-EQ Low Shelf
    static constexpr const char* paramPostEqLowFreq  = "postEqLowFreq";
    static constexpr const char* paramPostEqLowGain  = "postEqLowGain";
    static constexpr const char* paramPostEqLowQ     = "postEqLowQ";
    // Post-EQ Bell
    static constexpr const char* paramPostEqBellFreq = "postEqBellFreq";
    static constexpr const char* paramPostEqBellGain = "postEqBellGain";
    static constexpr const char* paramPostEqBellQ    = "postEqBellQ";
    // Post-EQ High Shelf
    static constexpr const char* paramPostEqHighFreq = "postEqHighFreq";
    static constexpr const char* paramPostEqHighGain = "postEqHighGain";
    static constexpr const char* paramPostEqHighQ    = "postEqHighQ";

    // Output stage
    static constexpr const char* paramOutputGain = "outputGain";
    static constexpr const char* paramCeiling    = "ceiling";
    static constexpr const char* paramDryWet     = "dryWet";

    // Master controls
    static constexpr const char* paramPreEqEnable  = "preEqEnable";
    static constexpr const char* paramPostEqEnable = "postEqEnable";
    static constexpr const char* paramBypass       = "bypass";

    //==============================================================================
    // Helper: read a float parameter by ID
    static float readFloatParam (juce::AudioProcessorValueTreeState& state,
                                 const char* id);

    //==============================================================================
    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    // Current sample rate
    double currentSampleRate = 44100.0;

    // Maximum block size for pre-allocations
    int maxSamplesPerBlock = 512;

    //==============================================================================
    // Atomic meter values for UI
    std::atomic<float> inputLevelDb    { 0.0f };
    std::atomic<float> outputLevelDb   { 0.0f };
    std::atomic<float> gainReductionDb { 0.0f };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZScorchAudioProcessor)
};