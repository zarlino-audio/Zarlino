#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "AnalysisCore.h"
#include "ControlLayer.h"
#include "Saturator.h"
#include "Tilt.h"
#include "Exciter.h"
#include "shared/licensing/ZLicenseManager.h"

using ZLicenseManager = Zarlino::Licensing::ZLicenseManager;

// Preset info struct (same as ZTame)
struct PresetInfo
{
    juce::String name;
    juce::String xmlState;
};

//==============================================================================
/** Number of frequency bands for multi-band processing. */
enum class BandCount
{
    Single = 0,  // Full-band processing (original behavior)
    Two    = 1,  // 2-band: low (< 250 Hz), high (≥ 250 Hz)
    Three  = 2,  // 3-band: low (< 250 Hz), mid (250 Hz – 1.5 kHz), high (≥ 1.5 kHz)
    Four   = 3   // 4-band: low (< 250 Hz), lo-mid (250 Hz – 1.5 kHz), hi-mid (1.5 kHz – 6 kHz), high (≥ 6 kHz)
};

//==============================================================================
/** Per-band DSP chain: independent Sat/Tilt/Exciter + crossover filters. */
struct BandChain
{
    static constexpr int kMaxBands = 4;

    // Crossover filters — each is 2 cascaded 2nd-order for LR4 24 dB/oct
    struct CrossoverFilter
    {
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                        juce::dsp::IIR::Coefficients<float>> stage1;
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                        juce::dsp::IIR::Coefficients<float>> stage2;

        void prepare (const juce::dsp::ProcessSpec& spec)
        {
            stage1.prepare (spec);
            stage2.prepare (spec);
        }

        void reset()
        {
            stage1.reset();
            stage2.reset();
        }

        void process (juce::dsp::ProcessContextReplacing<float>& ctx)
        {
            stage1.process (ctx);
            stage2.process (ctx);
        }
    };

    CrossoverFilter lpFilter;   // low-pass for this band
    CrossoverFilter hpFilter;   // high-pass for this band

    // Independent DSP modules per band
    Saturator saturator;
    Tilt      tilt;
    Exciter   exciter;

    // Band-specific control layer
    ControlLayer controlLayer;
    ControlVector bandCV;  // band-specific spectral features

    // Band gain (±6 dB)
    float bandGainLinear = 1.0f;

    void prepare (double sampleRate, int blockSize, int numChannels)
    {
        saturator.prepare (sampleRate, blockSize, numChannels);
        tilt.prepare (sampleRate, blockSize, numChannels);
        exciter.prepare (sampleRate, blockSize, numChannels);
        controlLayer.prepare (sampleRate);

        juce::dsp::ProcessSpec spec { sampleRate,
                                       static_cast<juce::uint32> (blockSize),
                                       static_cast<juce::uint32> (numChannels) };
        lpFilter.prepare (spec);
        hpFilter.prepare (spec);
    }

    void reset()
    {
        saturator.reset();
        tilt.reset();
        exciter.reset();
        controlLayer.reset();
        lpFilter.reset();
        hpFilter.reset();
    }
};

//==============================================================================
/**
    ZScorch v2.0 — adaptive multi-band harmonic processor.

    Continuously steers audio toward warmth, clarity and energy while keeping a
    musical, non-textbook saturation voice.

    v2.0 signal chain:
      Input → NaN/Inf guard
        → Pre-Emphasis EQ (low shelf · bell · high shelf)
        → Crossover split (1 / 2 / 3 / 4 bands; Single band skips the filters)
        → per band: Saturator (6 voiced curves, ADAA) → Tilt → Exciter
        → Crossover recombine + band gains
        → Post-Emphasis EQ
        → Master DC blocker
        → Output ceiling limiter (transparent below the knee)
        → Dry/Wet mix

    CPU notes (v2.0):
      * Saturator + Exciter run at base rate with antiderivative anti-aliasing —
        no oversampling filters unless the user enables the HQ 4x toggle.
      * Single-band mode has no crossover cost and processes in place.
      * EQ/crossover/shelf coefficients are change-gated (no audio-thread allocs).
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
    bool hasEditor() const override               { return true; }

    //==============================================================================
    const juce::String getName() const override   { return JucePlugin_Name; }
    bool acceptsMidi() const override             { return false; }
    bool producesMidi() const override            { return false; }
    bool isMidiEffect() const override            { return false; }
    double getTailLengthSeconds() const override  { return 0.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override       {}

    // Factory preset access
    const std::vector<PresetInfo>& getFactoryPresets() const { return factoryPresets; }
    int getCurrentPresetIndex() const noexcept { return currentPresetIndex; }
    void applyPresetByIndex (int index);

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Licensing
    ZLicenseManager& getLicenseManager() { return ZLicenseManager::getInstance(); }
    bool isLicensedForUse() const noexcept;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState()          { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // Cached parameter pointers for audio-thread access
    std::atomic<float>* liftParam                = nullptr;
    std::atomic<float>* characterParam           = nullptr;
    std::atomic<float>* mixParam                 = nullptr;

    // Saturator — per-band
    std::atomic<float>* satDriveLowParam         = nullptr;
    std::atomic<float>* satDriveLoMidParam       = nullptr;
    std::atomic<float>* satDriveHiMidParam       = nullptr;
    std::atomic<float>* satDriveHighParam        = nullptr;
    std::atomic<float>* satModeLowParam          = nullptr;
    std::atomic<float>* satModeLoMidParam        = nullptr;
    std::atomic<float>* satModeHiMidParam        = nullptr;
    std::atomic<float>* satModeHighParam         = nullptr;
    std::atomic<float>* satHarmRedLowParam       = nullptr;
    std::atomic<float>* satHarmRedLoMidParam     = nullptr;
    std::atomic<float>* satHarmRedHiMidParam     = nullptr;
    std::atomic<float>* satHarmRedHighParam      = nullptr;

    // Tilt
    std::atomic<float>* tiltAmountParam          = nullptr;

    // Exciter
    std::atomic<float>* exciterAmountParam       = nullptr;
    std::atomic<float>* exciterEnabledParam      = nullptr;

    // Multi-band
    std::atomic<float>* bandCountParam           = nullptr;  // 0=Single, 1=Two, 2=Three, 3=Four
    std::atomic<float>* bandGainLowParam         = nullptr;  // -6..+6 dB
    std::atomic<float>* bandGainLoMidParam       = nullptr;  // -6..+6 dB
    std::atomic<float>* bandGainHiMidParam       = nullptr;  // -6..+6 dB
    std::atomic<float>* bandGainHighParam        = nullptr;  // -6..+6 dB

    // Pre-Emphasis EQ (before crossover)
    std::atomic<float>* preEqLowShelfFreqParam   = nullptr;
    std::atomic<float>* preEqLowShelfGainParam   = nullptr;
    std::atomic<float>* preEqBellFreqParam       = nullptr;
    std::atomic<float>* preEqBellGainParam       = nullptr;
    std::atomic<float>* preEqBellQParam          = nullptr;
    std::atomic<float>* preEqHighShelfFreqParam  = nullptr;
    std::atomic<float>* preEqHighShelfGainParam  = nullptr;

    // Post-Emphasis EQ (after crossover recombine)
    std::atomic<float>* postEqLowShelfFreqParam  = nullptr;
    std::atomic<float>* postEqLowShelfGainParam  = nullptr;
    std::atomic<float>* postEqBellFreqParam      = nullptr;
    std::atomic<float>* postEqBellGainParam      = nullptr;
    std::atomic<float>* postEqBellQParam         = nullptr;
    std::atomic<float>* postEqHighShelfFreqParam = nullptr;
    std::atomic<float>* postEqHighShelfGainParam = nullptr;

    // Output
    std::atomic<float>* outputCeilingParam       = nullptr;  // -12..-0.1 dB
    std::atomic<float>* oversample4xParam        = nullptr;  // 0=2x, 1=4x

    // Metering: updated in processBlock, read by UI timer (atomic)
    std::atomic<float> meterLevel { 0.0f };

    // UI bypass flag (set from the editor; processBlock passes input through when true)
    std::atomic<bool> bypassEnabled { false };

    // Input/output level meters (normalized RMS, 0 dBFS ≈ 1.0) — UI reads via timer
    std::atomic<float> inputLevel  { 0.0f };
    std::atomic<float> outputLevel { 0.0f };

    // CPU load (per-block time-budget, smoothed) — UI reads via timer
    std::atomic<float> processingLoadPercent { 0.0f };
    float getProcessingLoadPercent() const noexcept { return processingLoadPercent.load (std::memory_order_relaxed); }

    //==============================================================================
    // Spectrum display interface for the UI (mirrors ZTame pattern).
    // Magnitudes normalized so 1.0 = 0 dBFS. Read (copy) from the UI thread.
    const std::vector<float>& getMagnitudeData() const noexcept
    {
        return analysisCore.getDisplayMagnitude();
    }

    int getNumBins() const noexcept { return analysisCore.getNumBins(); }

    float binToFrequency (int bin) const noexcept
    {
        return analysisCore.binToFrequency (bin, currentSampleRate);
    }

    /** Host sample rate (never elevated by internal oversampling). */
    double getHostSampleRate() const noexcept { return currentSampleRate; }

    /** Active band count: 1 = Single, 2 = Two, 3 = Three, 4 = Four. */
    int getActiveNumBands() const noexcept { return static_cast<int> (activeBandCount) + 1; }

    /** Undo/redo manager wired to the parameter state (UI undo/redo buttons). */
    juce::UndoManager* getUndoManager() { return undoManager.get(); }

private:
    //==============================================================================
    // Undo/redo history for parameter changes (must be declared before apvts so the
    // APVTS constructor can receive it)
    std::unique_ptr<juce::UndoManager> undoManager;

    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate    = 44100.0;
    int    currentSamplesPerBlock = 512;

    // Full-band DSP modules (used for Single band or as templates)
    AnalysisCore analysisCore;
    ControlLayer  controlLayer;

    // Multi-band chains
    BandCount activeBandCount = BandCount::Single;
    BandChain bands[BandChain::kMaxBands];

    // Dry buffer for wet/dry mix
    juce::AudioBuffer<float> dryBuffer;

    // Working buffers for multi-band splitting/recombining
    juce::AudioBuffer<float> bandBuffers[BandChain::kMaxBands];

    // Control-rate counter (update analysis every kControlInterval samples)
    static constexpr int kControlInterval = 256;
    int controlCounter = 0;

    // --- Helper: update crossover filter coefficients for a band ---
    void updateCrossoverFilters (double sampleRate);

    // --- Helper: update Pre-Emphasis EQ coefficients ---
    void updatePreEqFilters (double sampleRate);

    // --- Helper: update Post-Emphasis EQ coefficients ---
    void updatePostEqFilters (double sampleRate);

    // --- Helper: extract per-band control vector from FFT bins ---
    ControlVector extractBandCV (int bandIndex, const float* spectrum,
                                  int numBins, float binWidthHz) const;

    // --- Helper: apply output ceiling limiter ---
    void applyOutputCeiling (juce::AudioBuffer<float>& buffer, float ceilingDb);

    // --- Helper: update UI input/output RMS meters (normalized, 0 dBFS ≈ 1.0) ---
    void updateLevelMeters (const juce::AudioBuffer<float>& input,
                            const juce::AudioBuffer<float>& output,
                            int numChannels, int numSamples);

    // --- Helper: publish per-block CPU load (smoothed atomic) ---
    void publishProcessingLoad (juce::int64 blockStartTicks, int numSamples) noexcept;

    // Pre-Emphasis EQ filters (2 shelves + 1 bell × 2 channels)
    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                          juce::dsp::IIR::Coefficients<float>>;
    StereoFilter preEqLowShelf, preEqBell, preEqHighShelf;

    // Post-Emphasis EQ filters (2 shelves + 1 bell × 2 channels)
    StereoFilter postEqLowShelf, postEqBell, postEqHighShelf;

    // Factory presets
    std::vector<PresetInfo> factoryPresets;
    int currentPresetIndex = -1;

private:
    // Cached Pre/Post-EQ parameters to avoid heap allocation on audio thread.
    // change-detection gate: only call make*() when a value actually changed.
    float m_cachedPreLowShelfFreq  = -1.0f;
    float m_cachedPreLowShelfGain  = 0.0f;
    float m_cachedPreBellFreq      = -1.0f;
    float m_cachedPreBellGain      = 0.0f;
    float m_cachedPreBellQ         = -1.0f;
    float m_cachedPreHighShelfFreq = -1.0f;
    float m_cachedPreHighShelfGain = 0.0f;
    float m_cachedPreSampleRate    = 0.0;
    float m_cachedPostLowShelfFreq = -1.0f;
    float m_cachedPostLowShelfGain = 0.0f;
    float m_cachedPostBellFreq     = -1.0f;
    float m_cachedPostBellGain     = 0.0f;
    float m_cachedPostBellQ        = -1.0f;
    float m_cachedPostHighShelfFreq = -1.0f;
    float m_cachedPostHighShelfGain = 0.0f;
    float m_cachedPostSampleRate   = 0.0f;

    // --- Master DC blocker ---------------------------------------------------
    // Asymmetric saturation curves inject a tiny DC component; a single master
    // one-pole removes it before the ceiling stage so headroom is not lost.
    struct DcBlocker
    {
        float x1 = 0.0f, y1 = 0.0f;
        void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }
        float process (float x) noexcept
        {
            const float y = x - x1 + 0.9995f * y1;   // ~3.5 Hz at 44.1 kHz
            x1 = x; y1 = y;
            return y;
        }
    };
    static constexpr int kMaxProcChannels = 8;
    DcBlocker masterDc[kMaxProcChannels];
    bool lastHQActive = false;   // tracks "Oversample 4x" for latency reporting

    void initFactoryPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZScorchAudioProcessor)
};
