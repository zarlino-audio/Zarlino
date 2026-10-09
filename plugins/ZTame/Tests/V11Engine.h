#pragma once

// v1.1 engine, extracted from git for interleaved CPU A/B measurement only.
// This is NOT part of the shipped plugin.

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

//=============================================================================
/**
    ZTame ΓÇö spectral resonance suppressor (v1.1 engine).

    Removes narrow-band resonances and ringing harshness while leaving the
    surrounding tonal mass of the source intact: "Tame the ring. Keep the tone."

    Engine contract (see specs/ZTame_v1.1_rework_design.md):

      - STFT with sqrt-Hann analysis AND synthesis windows, 50 % overlap.
        sqrt(Hann) * sqrt(Hann) = Hann, and Hann + Hann(shift by N/2) = 1, so the
        overlap-add reconstructs perfectly with NO normalisation factor and no
        frame-rate amplitude ripple.
      - The wet path delay is exactly one frame (N samples), reported through
        setLatencySamples() and applied to every path (dry mix, listen, bypass)
        so host delay compensation and parallel mixing stay phase coherent.
      - Detection is a per-bin resonance RATIO against a frequency-adaptive local
        neighbourhood (never a plain level gate), with an optional sidechain bus
        as the detection source.
      - A draggable "focus band" (low/high frequency, soft skirts) restricts where
        suppression is allowed to happen. Replaces the inert v1.0 split-crossover.
      - Lock-free double-buffered telemetry for the editor: spectrum in/out,
        removed-energy curve, detected peak markers, gain-reduction metering.

    Signal flow:

        in ΓöÇΓû║ ring ΓöÇΓû║ [frame: sqrt-Hann ΓöÇΓû║ FFT ΓöÇΓû║ magnitude] ΓöÇΓöÉ
                                                              Γö£ΓöÇ detection ΓöÇΓû║
        sidechain ΓöÇΓû║ [same analysis] ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÿ   reduction dB
                                                                     Γöé
                                              focus band mask ΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöÇΓöñ
                                                                     Γû╝
                                       per-bin attack/release gain trajectory
                                                                     Γöé
                          frame * gain ΓöÇΓû║ IFFT ΓöÇΓû║ synthesis window ΓöÇΓû║ OLA ΓöÇΓû║ wet
                                                                     Γöé
        out = (1-mix) * dry[N] + mix * wet        (listen: out = delta * 2)
*/
class ZTameV11Processor final : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    //=============================================================================
    /** Lock-free snapshot of everything the editor needs to draw one frame. */
    struct UiSnapshot
    {
        static constexpr int numDisplayBins = 256;
        static constexpr int maxPeaks       = 24;

        std::array<float, numDisplayBins> inputDb      {};  // main input spectrum (dBFS)
        std::array<float, numDisplayBins> outputDb     {};  // processed spectrum (dBFS)
        std::array<float, numDisplayBins> reductionDb  {};  // removed energy (dB, positive)
        std::array<float, numDisplayBins> sidechainDb  {};  // sidechain spectrum (dBFS)
        std::array<float, maxPeaks>       peakFreqHz   {};
        std::array<float, maxPeaks>       peakReductionDb {};
        int   numPeaks          = 0;
        float grAverageDb       = 0.0f;
        float grPeakDb          = 0.0f;
        float thresholdDb       = 12.0f;   // detector trigger currently required
        int   fftSize           = 2048;
        double sampleRate       = 44100.0;
        bool  listenMode        = false;
        bool  bypassed          = false;
        bool  sidechainOn       = false;
        bool  sidechainConnected= false;
        bool  sidechainLive     = false;   // bus connected AND carrying signal
        bool  midSide           = false;
    };

    //=============================================================================
    ZTameV11Processor();
    ~ZTameV11Processor() override;

    //=============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //=============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return false; }

    //=============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //=============================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //=============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //=============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    //=============================================================================
    // Telemetry / editor support (all safe to call from the message thread)
    bool  getLatestSnapshot (UiSnapshot& dest) const;
    void  setUiActive (bool shouldBeActive) noexcept { uiActive.store (shouldBeActive); }
    float getGainReductionDb() const noexcept        { return grAverageDb.load(); }
    float getGainReductionPeakDb() const noexcept    { return grPeakDb.load(); }
    bool  isSidechainConnected() const;
    bool  isSidechainLive() const noexcept           { return sidechainLive.load(); }
    int   getActiveFftSize() const noexcept          { return activeFftSize.load(); }

    //=============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Log frequency Γåö 0..1 display mapping (20 Hz ΓÇª 20 kHz). Shared with the editor
    // so the draggable focus band maps to exactly the same axis as the analyser.
    static float freqToDisplayNorm (float freqHz) noexcept;
    static float displayNormToFreq (float norm) noexcept;

    // Parameter IDs (stable ΓÇö never rename)
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
    static constexpr const char* paramMidSide     = "midSide";

private:
    //=============================================================================
    // Frame geometry
    static constexpr int minFftOrder = 10;                    //  1024 ΓåÆ 23.2 ms @ 44.1k
    static constexpr int maxFftOrder = 11;                    //  2048 ΓåÆ 46.4 ms @ 44.1k
    static constexpr int maxFftSize  = 1 << maxFftOrder;      //  2048
    static constexpr int maxNumBins  = (maxFftSize / 2) + 1;  //  1025
    static constexpr int ringSize    = 2 * maxFftSize;        //  4096

    //=============================================================================
    struct ChannelState
    {
        std::vector<float> inRing;       // ringSize    ΓÇö input history
        std::vector<float> outAcc;       // ringSize    ΓÇö wet overlap-add accumulator
        std::vector<float> dryRing;      // ringSize    ΓÇö input delayed by N (latency-aligned dry)
        std::vector<float> fftData;      // 2*maxFftSize ΓÇö JUCE real FFT scratch (must be 2N floats)
        std::vector<float> magnitude;    // maxNumBins  ΓÇö |X[k]|
        std::vector<float> holdMag;      // maxNumBins  ΓÇö detector peak-hold
        std::vector<float> reductionDb;  // maxNumBins  ΓÇö target reduction (dB, positive)
        std::vector<float> targetGain;   // maxNumBins  ΓÇö linear target gain
        std::vector<float> gainSmooth;   // maxNumBins  ΓÇö attack/release smoothed gain
        bool  prepared = false;
    };

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
        bool  midSide      = false;
        int   fftOrder     = maxFftOrder;
    };

    //=============================================================================
    // Core DSP
    void  processBlockInternal (juce::AudioBuffer<float>& buffer, bool hostBypass);
    void  initChannel (ChannelState& state, int fftOrder);
    void  resetChannel (ChannelState& state) noexcept;      // audio-thread safe (no allocation)
    void  resetWetState (ChannelState& state) noexcept;     // zero the OLA/gains, keep the delay lines
    Params readParams() const noexcept;
    void  runHop();                                         // one STFT frame (all channels)
    void  analyseChannel (ChannelState& state, int fftOrder) noexcept;
    void  computeReductionSpectrum (const float* mag,
                                    std::vector<float>& reductionOut,
                                    const Params& p) noexcept;
    void  synthesiseChannel (ChannelState& state, int fftOrder,
                             const Params& p, float attackCoeff, float releaseCoeff,
                             float& avgReductionDb, float& peakReductionDb) noexcept;
    void  publishSnapshot (const Params& p, bool scLive) noexcept;
    void  rebuildDisplayLookup (int fftSize) noexcept;

    float detectorThresholdDb (const Params& p) const noexcept;

    static int   neighbourhoodRadius (int binIndex, double sampleRate, int fftSize) noexcept;
    static float bandProfile (float freqHz, float lowHz, float highHz) noexcept;

    //=============================================================================
    void timerCallback() override;

    //=============================================================================
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
    std::atomic<float>* pMidSide     = nullptr;

    std::unique_ptr<juce::dsp::FFT> fftMin;   // order 10
    std::unique_ptr<juce::dsp::FFT> fftMax;   // order 11

    std::array<ChannelState, 2> mainChannels;
    std::array<ChannelState, 2> sideChannels;

    // Pre-computed windows, one per supported FFT order
    std::array<std::vector<float>, maxFftOrder + 1> windows;
    std::array<float, maxFftOrder + 1> windowGains {};   // coherent gain (mean) of each window

    // Shared scratch (audio thread only)
    std::vector<float> dbScratch;        // maxNumBins
    std::vector<float> prefixScratch;    // maxNumBins + 1
    std::vector<float> detectionMag;     // maxNumBins ΓÇö combined detection source
    std::vector<float> sharedReduction;  // maxNumBins ΓÇö stereo-linked reduction
    std::vector<float> displayIn;        // UiSnapshot::numDisplayBins
    std::vector<float> displayOut;       // UiSnapshot::numDisplayBins
    std::vector<float> displayRed;       // UiSnapshot::numDisplayBins
    std::vector<float> displaySide;      // UiSnapshot::numDisplayBins
    std::vector<int>   displayLookup;    // UiSnapshot::numDisplayBins + 1 ΓÇö FFT bin edges

    // Smoothed wet/dry mix (per-sample, message-thread parameter ΓåÆ audio thread)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> mixSmooth;

    // Transport / state
    double currentSampleRate = 44100.0;
    int    maxSamplesPerBlock = 512;
    int    samplesSinceHop = 0;
    int    ringPos = 0;                 // next ring slot to write (also the frame end)
    int    numMainChannels = 0;
    int    numSideChannels = 0;         // sidechain channels analysed this block
    bool   prepared = false;
    bool   wasBypassed = false;
    bool   bypassDeep = false;          // bypassed long enough that the engine is parked
    int    bypassHoldSamples = 0;
    int    silentSidechainHops = 0;
    bool   sidechainInUse = false;      // this block: sidechain is the detection source

    std::atomic<int>  activeFftOrder { maxFftOrder };
    std::atomic<int>  activeFftSize { maxFftSize };
    std::atomic<bool> uiActive { false };
    std::atomic<bool> sidechainLive { false };
    int sidechainSilentHopCount = 0;

    // Metering
    std::atomic<float> grAverageDb { 0.0f };
    std::atomic<float> grPeakDb    { 0.0f };
    float grAverageSmoothed = 0.0f;
    float grPeakSmoothed    = 0.0f;
    int   snapshotThrottle   = 0;

    // Lock-free double-buffered UI snapshot
    std::array<UiSnapshot, 2> snapshotSlots;
    std::atomic<int> snapshotWriteIndex { 0 };    // slot the audio thread will fill next
    std::atomic<int> snapshotReadyIndex { -1 };   // last published slot
    mutable std::atomic<int> snapshotReadIndex { -1 };   // slot the UI is currently copying

    //=============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZTameV11Processor)
};



