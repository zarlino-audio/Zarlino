#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>
#include <memory>

//==============================================================================
/**
    ZTame — High-resolution automatic resonance suppressor.

    Continuously analyses the input spectrum for resonant peaks that exceed a
    sensitivity threshold, and applies narrow, dynamic, per-peak gain reduction
    only to those specific resonances as they appear and disappear.

    Signal flow:
        1. High-resolution FFT-based spectral analysis identifies peaks whose
           magnitude stands out sharply against their local spectral neighbourhood.
        2. Each detected peak triggers a narrow, dynamic notch engaged only while
           that specific peak exceeds the sensitivity threshold.
        3. A Selectivity control governs how sharply defined a peak must be to
           trigger suppression, protecting natural tonal character.
        4. Optional split-band operation prevents low-band suppression from
           being triggered by high-frequency content, and vice versa.
        5. Listen/Solo mode auditions only the suppressed material.
        6. Dry/Wet mix for parallel processing.
 */
class ZTameAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    ZTameAudioProcessor();
    ~ZTameAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.1; }

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
    float getGainReductionDb() const { return gainReductionDb.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    //==============================================================================
    // FFT processing constants
    static constexpr int fftOrder  = 11;   // 2048-point FFT
    static constexpr int fftSize   = 1 << fftOrder;
    static constexpr int fftHalf   = fftSize / 2;
    static constexpr int numBins   = fftHalf + 1;   // bins in real FFT magnitude
    static constexpr int hopSize   = fftSize / 2;    // 50% overlap — COLA-safe with Hann
    static constexpr int windowSize = fftSize;

    //==============================================================================
    // Per-channel processing state
    struct ChannelState
    {
        // Ring buffer for input accumulation
        std::vector<float> inputBuffer;
        int inputWritePos = 0;

        // Overlap-add output accumulator
        std::vector<float> outputAccum;
        int outputWritePos = 0;

        // FFT work buffer (real data, size fftSize)
        std::vector<float> fftBuffer;

        // Hann window coefficients
        std::vector<float> window;

        // Per-bin gain state with smoothing
        std::vector<float> binGain;
        std::vector<float> binGainSmooth;

        // Spectral magnitude of previous frame (for peak persistence)
        std::vector<float> magnitude;

        // Samples written since last FFT
        int samplesSinceFFT = 0;

        // Initialisation flag
        bool prepared = false;
    };

    //==============================================================================
    // Parameter IDs
    static constexpr const char* paramSensitivity   = "sensitivity";
    static constexpr const char* paramRange         = "range";
    static constexpr const char* paramSelectivity   = "selectivity";
    static constexpr const char* paramCrossoverFreq = "crossoverFreq";
    static constexpr const char* paramAttack        = "attack";
    static constexpr const char* paramRelease       = "release";
    static constexpr const char* paramListenMode    = "listenMode";
    static constexpr const char* paramDryWet        = "dryWet";
    static constexpr const char* paramBypass        = "bypass";

    //==============================================================================
    // Helper: read a float parameter by ID
    static float readFloatParam (juce::AudioProcessorValueTreeState& state,
                                 const char* id);

    //==============================================================================
    // Core DSP methods
    void initChannelState (ChannelState& state, double sampleRate);
    void processFrame (ChannelState& state, int channelIndex);

    // Spectral analysis and peak detection
    void computeMagnitude (const std::vector<float>& spectrum,
                           std::vector<float>& magnitude) const;

    // Local-neighbourhood peak detection and gain computation
    void computeBinGains (const std::vector<float>& magnitude,
                          std::vector<float>& gains,
                          double sampleRate,
                          float sensitivity,
                          float rangeFactor,
                          float selectivityFactor,
                          float crossoverHz,
                  bool splitBandActive) const;

    // Apply gains to complex FFT bins, then IFFT
    void applyGainsAndSynthesise (const std::vector<float>& gains,
                                  std::vector<float>& spectrum) const;

    //==============================================================================
    // Parameter state
    juce::AudioProcessorValueTreeState parameters;

    //==============================================================================
    // DSP state per channel (stereo)
    std::array<ChannelState, 2> channels;

    // Shared FFT object (stateless, can be used per-channel)
    std::unique_ptr<juce::dsp::FFT> fft;

    // Current sample rate
    double currentSampleRate = 44100.0;

    // Maximum block size for pre-allocations
    int maxSamplesPerBlock = 512;

    //==============================================================================
    // Atomic meter values for UI
    std::atomic<float> gainReductionDb { 0.0f };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameAudioProcessor)
};
