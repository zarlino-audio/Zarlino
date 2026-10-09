#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

//==============================================================================
/**
    ZTame — zero-latency resonance suppressor (v1.2 engine).

    Removes narrow-band resonances and ringing harshness while leaving the
    surrounding tonal mass of the source intact: "Tame the ring. Keep the tone."

    Engine contract (see specs/ZTame_v1.2_zero-latency_design.md):

      - ZERO latency. The correction runs entirely in the time domain and the
        detection window covers only PAST input samples, so output sample n
        depends only on input samples <= n and setLatencySamples(0) is reported
        for every path — host bypass included.
      - Bit-transparent when idle: the correction chain is a cascade of RBJ
        peaking biquads whose 0.000 dB form has identical numerator and
        denominator coefficients, so an inactive engine passes audio through
        sample-exactly (residual = 0, not merely "low").
      - Detection keeps the v1.1 detector — per-bin resonance RATIO against a
        frequency-adaptive neighbourhood baseline, focus band mask, peak hold,
        selectivity — but the transform is analysis-only: there is no inverse
        FFT, no synthesis window, no overlap-add, and no per-bin gain applied to
        the audio. Per-band corrections are executed by the bell cascade.
      - Correction aggressiveness comes from the bells: a bell hits its
        specified depth exactly at its centre, so a detected resonance is
        reduced by the full clamped amount instead of the partial cancellation
        a subtracted-band filterbank would deliver.
      - The engine parks itself when it provably cannot change the output
        (silence, nothing detected, bypass), so idle CPU approaches zero.

    Signal flow:

        in ─┬─ correction : cascade of N peaking bells (cuts only) ──► wet
            │                  ▲
            │                  │ per-band gains (attack/release, dB)
            │        ┌─────────┴───────────┐
            │        │ band aggregation    │  max reduction per band
            │        └─────────▲───────────┘
            │                  │
            └─ analysis : past-window FFT (N = 1024, hop = N/2, Hann)
                          ─► per-bin magnitude + 80 ms peak hold
                          ─► excess over local neighbourhood
                          ─► threshold · focus mask · selectivity

        out = x + (wet - x) * mix          (listen: out = (x - wet) * 2)
*/
class ZTameAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    /** Lock-free snapshot of everything the editor needs to draw one frame. */
    struct UiSnapshot
    {
        static constexpr int numDisplayBins = 256;
        static constexpr int maxPeaks       = 24;

        std::array<float, numDisplayBins> inputDb      {};  // main input spectrum (dBFS)
        std::array<float, numDisplayBins> outputDb     {};  // processed spectrum (dBFS)
        std::array<float, numDisplayBins> reductionDb  {};  // detected reduction (dB, positive)
        std::array<float, numDisplayBins> sidechainDb  {};  // sidechain spectrum (dBFS)
        std::array<float, maxPeaks>       peakFreqHz   {};
        std::array<float, maxPeaks>       peakReductionDb {};
        int   numPeaks          = 0;
        float grAverageDb       = 0.0f;
        float grPeakDb          = 0.0f;
        float thresholdDb       = 12.0f;   // detector trigger currently required
        int   bandCount         = 32;
        double sampleRate       = 44100.0;
        bool  listenMode        = false;
        bool  bypassed          = false;
        bool  sidechainOn       = false;
        bool  sidechainConnected= false;
        bool  sidechainLive     = false;   // bus connected AND carrying signal
        int   processingMode    = 0;
        float outputLevelDb     = -100.0f; // peak of the block just processed
        float processingLoad    = 0.0f;    // % of the block's real-time budget used
    };

    //==============================================================================
    ZTameAudioProcessor();
    ~ZTameAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    // Factory programs: index 0 is "Init", 1..6 the six factory presets.
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    //==============================================================================
    // Telemetry / editor support (all safe to call from the message thread)
    bool  getLatestSnapshot (UiSnapshot& dest) const;
    void  setUiActive (bool shouldBeActive) noexcept { uiActive.store (shouldBeActive); }
    float getGainReductionDb() const noexcept        { return grAverageDb.load(); }
    float getGainReductionPeakDb() const noexcept    { return grPeakDb.load(); }
    bool  isSidechainConnected() const;
    bool  isSidechainLive() const noexcept           { return sidechainLive.load(); }

    /** Copies the newest published frame into the internal UI cache and returns
        the analysis spectrum (UiSnapshot::numDisplayBins bins, dBFS, 20 Hz..20 kHz
        log-spaced), or nullptr until the first frame has been published.

        Call this first: the other data accessors below read the same cached frame,
        so a display never mixes arrays from two different analysis frames. */
    const float* getMagnitudeData() const noexcept;
    /** Detected-reduction spectrum (dB, positive) from the same frame as getMagnitudeData(). */
    const float* getSuppressionData() const noexcept;
    int    getNumBins() const noexcept      { return UiSnapshot::numDisplayBins; }
    double getHostSampleRate() const noexcept { return currentSampleRate; }
    /** Peak level of the most recent block, dBFS (smoothed release). */
    float  getOutputLevelDb() const noexcept  { return outputLevelDb.load(); }
    /** Share of the last block's real-time budget spent in the engine, percent. */
    float  getProcessingLoadPercent() const noexcept { return processingLoadPercent.load(); }

    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Log frequency ↔ 0..1 display mapping (20 Hz … 20 kHz). Shared with the editor
    // so the draggable focus band maps to exactly the same axis as the analyser.
    static float freqToDisplayNorm (float freqHz) noexcept;
    static float displayNormToFreq (float norm) noexcept;

    // Parameter IDs (stable — never rename)
    static constexpr const char* paramSensitivity = "sensitivity";
    static constexpr const char* paramRange       = "range";
    static constexpr const char* paramSelectivity = "selectivity";
    static constexpr const char* paramFocusLow    = "focusLow";
    static constexpr const char* paramFocusHigh   = "focusHigh";
    static constexpr const char* paramAttack      = "attack";
    static constexpr const char* paramRelease     = "release";
    static constexpr const char* paramListenMode  = "listenMode";
    static constexpr const char* paramDryWet      = "dryWet";
    static constexpr const char* paramBypass      = "bypass";
    static constexpr const char* paramSidechain   = "sidechain";
    static constexpr const char* paramStereoLink  = "stereoLink";
    static constexpr const char* paramResolution  = "resolution";
    static constexpr const char* paramProcessingMode = "processingMode";

    /** Processing modes (index order matters — it is saved in presets). */
    enum ProcessingMode
    {
        modeStereo = 0,
        modeMidSide = 1,
        modeMidOnly = 2,
        modeSideOnly = 3
    };

    static constexpr int numFactoryPrograms = 7;   // Init + six factory presets

private:
    //==============================================================================
    // Engine geometry
    static constexpr int   maxBands      = 48;                   // "Precise" band count
    static constexpr int   analysisOrder = 10;                   // 1024-point analysis FFT
    static constexpr int   analysisN     = 1 << analysisOrder;
    static constexpr int   analysisHop   = analysisN;            // control rate ≈ 21.7 ms @ 47k
    static constexpr int   analysisBins  = analysisN / 2 + 1;
    static constexpr int   analysisRing  = 2 * analysisN;
    static constexpr float kMinFreq      = 20.0f;
    static constexpr float kMaxFreq      = 20000.0f;
    static constexpr float kBellQ        = 2.0f;                 // ≈ 0.5-octave bells

    //==============================================================================
    /** One peaking biquad of the correction cascade (direct form I, normalized).

        At 0.000 dB the RBJ peaking coefficients degenerate to b == a, and with
        seeded/aligned states the stage computes y = x exactly — that is what
        makes an idle engine a bit-exact passthrough. */
    struct BellBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

        inline float process (float x) noexcept
        {
            const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        /** Start from a steady state: with x1=x2=y1=y2=input the first output is
            exactly the input for ANY peaking coefficients (unity DC gain), so a
            band can switch on without a click. */
        inline void seed (float x) noexcept
        {
            x1 = x2 = y1 = y2 = x;
        }

        inline void setIdentity() noexcept
        {
            b0 = 1.0f; b1 = 0.0f; b2 = 0.0f; a1 = 0.0f; a2 = 0.0f;
        }

        /** Set a cut-only peaking response. cutDb >= 0; cosW0 and alpha are the
            band's precomputed constants (cos(w0), sin(w0)/(2Q)). */
        inline void setCut (float cutDb, float cosW0, float alpha) noexcept
        {
            const float A   = juce::Decibels::decibelsToGain (-cutDb * 0.5f);
            const float a0  = 1.0f + alpha / A;
            const float inv = 1.0f / a0;

            b0 = (1.0f + alpha * A) * inv;
            b1 = (-2.0f * cosW0) * inv;
            b2 = (1.0f - alpha * A) * inv;
            a1 = (-2.0f * cosW0) * inv;
            a2 = (1.0f - alpha / A) * inv;
        }
    };

    //==============================================================================
    struct ChannelState
    {
        // Correction (the only part of the engine that touches the audio path)
        BellBiquad bells[maxBands];
        float cutDb[maxBands]        {};   // smoothed reduction, dB >= 0
        float appliedCutDb[maxBands] {};   // cut the coefficients were built from
        bool  seedPending[maxBands]  {};   // band just switched on → seed next sample
        bool  anyCut = false;              // any band currently cutting

        // Analysis — runs on PAST samples only; never in the audio path
        float inRing[analysisRing] {};
        int   ringPos = 0;
        float fftData[2 * analysisN] {};
        float mag[analysisBins]      {};   // last frame magnitude (linear, full-scale ref)
        float holdMag[analysisBins]  {};   // 80 ms peak hold
        float bandLevel[maxBands]    {};   // per-band level (metering weights)

        bool prepared = false;
    };

    //==============================================================================
    /** Everything that depends on the band count / sample rate. Rebuilt whenever
        the resolution parameter or the sample rate changes. No allocation. */
    struct BandPlan
    {
        int numBands = 32;
        std::array<float, maxBands> centerHz {};
        std::array<float, maxBands> cosW0    {};
        std::array<float, maxBands> alpha    {};   // sin(w0) / (2Q)

        /** FFT bin → correction band (-1 = outside the band range). */
        std::array<int, analysisBins> binToBand {};

        /** Display bin → FFT bin edges (log axis, 20 Hz..20 kHz). */
        std::array<int, UiSnapshot::numDisplayBins + 1> displayLookup {};
    };

    //==============================================================================
    struct Params
    {
        float sensitivity  = 65.0f;
        float rangeDb      = 12.0f;
        float selectivity  = 50.0f;
        float focusLowHz   = 20.0f;
        float focusHighHz  = 20000.0f;
        float attackMs     = 8.0f;
        float releaseMs    = 180.0f;
        float mix          = 1.0f;
        bool  listening    = false;
        bool  bypassed     = false;
        bool  sidechainOn  = false;
        bool  stereoLink   = true;
        int   processingMode = modeStereo;
        int   planIndex    = 0;            // 0 = Standard (32), 1 = Precise (48)
    };

    //==============================================================================
    // Core DSP
    void  processBlockInternal (juce::AudioBuffer<float>& buffer, bool hostBypass);
    void  rebuildBandPlan (int planIndex) noexcept;
    void  resetChannel (ChannelState& state) noexcept;
    void  resetAllChannels() noexcept;
    void  analyseChannel (ChannelState& state) noexcept;
    void  computeReductionSpectrum (const float* holdMag, std::vector<float>& out,
                                    const Params& p) noexcept;
    void  runHop (const Params& p, const bool* channelProcessed,
                  const bool* scProcessed, bool scArmed) noexcept;
    void  publishSnapshot (const Params& p, bool scLive) noexcept;
    void  computeBandLevels (ChannelState& state) noexcept;
    Params readParams() const noexcept;

    float detectorThresholdDb (const Params& p) const noexcept;

    static int   neighbourhoodRadius (int binIndex, double sampleRate, int fftSize) noexcept;
    static float bandProfile (float freqHz, float lowHz, float highHz) noexcept;

    //==============================================================================
    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* pSensitivity = nullptr;
    std::atomic<float>* pRange       = nullptr;
    std::atomic<float>* pSelectivity = nullptr;
    std::atomic<float>* pFocusLow    = nullptr;
    std::atomic<float>* pFocusHigh   = nullptr;
    std::atomic<float>* pAttack      = nullptr;
    std::atomic<float>* pRelease     = nullptr;
    std::atomic<float>* pListen      = nullptr;
    std::atomic<float>* pDryWet      = nullptr;
    std::atomic<float>* pBypass      = nullptr;
    std::atomic<float>* pSidechain   = nullptr;
    std::atomic<float>* pStereoLink  = nullptr;
    std::atomic<float>* pResolution  = nullptr;
    std::atomic<float>* pProcessingMode = nullptr;

    // Engine state
    std::array<ChannelState, 2> mainChannels;
    std::array<ChannelState, 2> sideChannels;
    BandPlan bandPlan;

    // Analysis engine (shared; the transform itself is stateless after construction)
    std::unique_ptr<juce::dsp::FFT> analysisFft;
    std::vector<float> window;
    float windowGain = 1.0f;

    // Analysis scratch (audio thread only)
    std::vector<float> dbScratch;          // analysisBins
    std::vector<float> prefixScratch;      // analysisBins + 1
    std::vector<float> combinedHold;       // analysisBins — linked detection source
    std::vector<float> detectionReduction; // analysisBins — latest reduction (display source)
    std::vector<float> displayIn;          // UiSnapshot::numDisplayBins
    std::vector<float> displayOut;
    std::vector<float> displayRed;
    std::vector<float> displaySide;

    // Smoothed wet/dry mix (per-sample, message-thread parameter → audio thread)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmooth;

    // Transport / state
    double currentSampleRate = 44100.0;
    int    maxSamplesPerBlock = 512;
    int    activePlanIndex = -1;
    int    numMainChannels = 0;
    int    numSideChannels = 0;         // sidechain channels analysed this block
    int    samplesUntilHop = analysisHop;
    bool   prepared = false;
    bool   wasBypassed = false;

    // Per-hop coefficients (recomputed every hop from the parameters / sample rate)
    float holdCoeff    = 0.9f;
    float attackCoeff  = 1.0f;
    float releaseCoeff = 0.1f;

    // Sidechain liveness (hops, not seconds)
    int  sidechainSilentHops = 0;
    bool sidechainLiveInternal = false;

    std::atomic<bool> uiActive { false };
    std::atomic<bool> sidechainLive { false };

    // Metering
    std::atomic<float> grAverageDb { 0.0f };
    std::atomic<float> grPeakDb    { 0.0f };
    std::atomic<float> outputLevelDb { -100.0f };
    std::atomic<float> processingLoadPercent { 0.0f };
    float grAverageSmoothed = 0.0f;
    float grPeakSmoothed    = 0.0f;

    // Snapshot publishing
    int publishCountdown = 0;
    std::array<UiSnapshot, 2> snapshotSlots;
    std::atomic<int> snapshotWriteIndex { 0 };    // slot the audio thread will fill next
    std::atomic<int> snapshotReadyIndex { -1 };   // last published slot
    mutable std::atomic<int> snapshotReadIndex { -1 };   // slot the UI is currently copying

    // Local frame cache for the editor so that several arrays can be read from one
    // frame without copying it more than once per repaint.
    mutable UiSnapshot uiCache;
    mutable bool uiCacheValid = false;

    // Program state
    std::atomic<int> currentProgram { 0 };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameAudioProcessor)
};
