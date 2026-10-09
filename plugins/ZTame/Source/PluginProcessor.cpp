#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

//==============================================================================
namespace
{
    constexpr float kSilenceFloorDb = -140.0f;
    constexpr float kBandGateDb     = -90.0f;    // bins quieter than this are never touched
    constexpr float kSkirtRatio     = 0.2599f;   // 2^(1/3) - 1 → one-third-octave band skirts
    constexpr float kListenMonitor  = 2.0f;      // +6 dB on the removed-material audition
    constexpr float kHoldSeconds    = 0.080f;    // detector per-bin peak hold
    constexpr float kIdentitySnapDb = 0.0005f;   // cuts below this are exactly 0 → bit-transparency
    constexpr float kProcessFloorDb = 0.2000f;   // cuts below this are inaudible → skip the filter
    constexpr float kSilenceGateDb  = -90.0f;    // whole-block gate for the idle fast path

    inline float sanitise (float v) noexcept
    {
        return std::isfinite (v) ? v : 0.0f;
    }

    inline float probeDb (float g, float floor) noexcept
    {
        return juce::Decibels::gainToDecibels (g, floor);
    }

    inline float smoothStep (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t * t * (3.0f - 2.0f * t);
    }

    /** One-pole coefficient for a time constant expressed in frames (hops). */
    inline float frameCoeff (float millis, float hopSeconds) noexcept
    {
        const float t = juce::jmax (5.0e-5f, millis * 0.001f);
        return 1.0f - std::exp (-hopSeconds / t);
    }

    //==============================================================================
    struct FactoryPreset
    {
        const char* name;
        float sensitivity, rangeDb, selectivity, focusLow, focusHigh, attackMs, releaseMs;
        int   planIndex;                          // 0 = Standard (32 bands), 1 = Precise (48)
    };

    const FactoryPreset kFactoryPrograms[] =
    {
        { "Init",         65.0f, 12.0f, 50.0f,    20.0f, 20000.0f,   8.0f, 180.0f, 0 },
        { "Vocal Soothe", 70.0f, 10.0f, 62.0f,  1500.0f,  9000.0f,   6.0f, 160.0f, 1 },
        { "Room Tamer",   75.0f, 14.0f, 40.0f,   150.0f,  6000.0f,  12.0f, 220.0f, 0 },
        { "Drum Ring",    72.0f, 12.0f, 55.0f,   500.0f,  8000.0f,   4.0f, 120.0f, 0 },
        { "Master Polish",55.0f,  6.0f, 45.0f,    20.0f, 20000.0f,  15.0f, 250.0f, 1 },
        { "De-Harsh",     68.0f,  9.0f, 58.0f,  3000.0f, 16000.0f,   5.0f, 140.0f, 1 },
        { "Extreme Ring", 85.0f, 20.0f, 70.0f,  2000.0f, 12000.0f,   2.0f,  80.0f, 1 },
    };

    static_assert ((sizeof (kFactoryPrograms) / sizeof (kFactoryPrograms[0]))
                       == (size_t) ZTameAudioProcessor::numFactoryPrograms,
                   "factory program table size mismatch");
}

//==============================================================================
ZTameAudioProcessor::ZTameAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZTameState", createParameterLayout())
{
    // Analysis-only FFT. The transform is stateless after construction; the
    // frame buffers live in each ChannelState (audio-thread only).
    analysisFft = std::make_unique<juce::dsp::FFT> (analysisOrder);

    // sqrt(Hann) — the same analysis window family as v1.1, so the magnitude
    // calibration (and therefore the detector thresholds) carries over.
    window.resize ((size_t) analysisN);

    double windowSum = 0.0;
    for (int i = 0; i < analysisN; ++i)
    {
        const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                * (double) i / (double) analysisN);
        window[(size_t) i] = (float) std::sqrt (juce::jmax (0.0, hann));
        windowSum += window[(size_t) i];
    }
    windowGain = (float) (windowSum / (double) analysisN);

    dbScratch.assign          (analysisBins, 0.0f);
    prefixScratch.assign      (analysisBins + 1, 0.0f);
    combinedHold.assign       (analysisBins, 0.0f);
    detectionReduction.assign (analysisBins, 0.0f);
    displayIn.assign          (UiSnapshot::numDisplayBins, kSilenceFloorDb);
    displayOut.assign         (UiSnapshot::numDisplayBins, kSilenceFloorDb);
    displayRed.assign         (UiSnapshot::numDisplayBins, 0.0f);
    displaySide.assign        (UiSnapshot::numDisplayBins, kSilenceFloorDb);

    // Cached parameter pointers — no string lookups anywhere near processBlock
    pSensitivity = parameters.getRawParameterValue (paramSensitivity);
    pRange       = parameters.getRawParameterValue (paramRange);
    pSelectivity = parameters.getRawParameterValue (paramSelectivity);
    pFocusLow    = parameters.getRawParameterValue (paramFocusLow);
    pFocusHigh   = parameters.getRawParameterValue (paramFocusHigh);
    pAttack      = parameters.getRawParameterValue (paramAttack);
    pRelease     = parameters.getRawParameterValue (paramRelease);
    pListen      = parameters.getRawParameterValue (paramListenMode);
    pDryWet      = parameters.getRawParameterValue (paramDryWet);
    pBypass      = parameters.getRawParameterValue (paramBypass);
    pSidechain   = parameters.getRawParameterValue (paramSidechain);
    pStereoLink  = parameters.getRawParameterValue (paramStereoLink);
    pResolution  = parameters.getRawParameterValue (paramResolution);
    pProcessingMode = parameters.getRawParameterValue (paramProcessingMode);
}

ZTameAudioProcessor::~ZTameAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZTameAudioProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto detectGroup = std::make_unique<AudioProcessorParameterGroup> ("detect", "Detect", "|");
    detectGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramSensitivity, "Sensitivity",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 65.0f,
        AudioParameterFloatAttributes().withLabel ("%")
            .withCategory (AudioProcessorParameter::genericParameter)));
    detectGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramSelectivity, "Selectivity",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    detectGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramFocusLow, "Focus Low",
        NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 20.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    detectGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramFocusHigh, "Focus High",
        NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), 20000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));
    detectGroup->addChild (std::make_unique<AudioParameterBool> (
        paramSidechain, "Sidechain Detect", false));
    detectGroup->addChild (std::make_unique<AudioParameterBool> (
        paramStereoLink, "Stereo Link", true));
    layout.add (std::move (detectGroup));

    auto shapeGroup = std::make_unique<AudioProcessorParameterGroup> ("shape", "Shape", "|");
    shapeGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramRange, "Depth",
        NormalisableRange<float> (0.0f, 24.0f, 0.05f, 0.7f), 12.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    shapeGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramAttack, "Attack",
        NormalisableRange<float> (0.1f, 100.0f, 0.05f, 0.4f), 8.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    shapeGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramRelease, "Release",
        NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.5f), 180.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    shapeGroup->addChild (std::make_unique<AudioParameterChoice> (
        paramResolution, "Bands",
        StringArray { "Standard (32)", "Precise (48)" }, 0));
    layout.add (std::move (shapeGroup));

    auto outputGroup = std::make_unique<AudioProcessorParameterGroup> ("output", "Output", "|");
    outputGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramDryWet, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    outputGroup->addChild (std::make_unique<AudioParameterChoice> (
        paramProcessingMode, "Processing Mode",
        StringArray { "Stereo", "M/S Blend", "Mid Only", "Side Only" }, 0));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramListenMode, "Delta (Listen)", false));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramBypass, "Bypass", false));
    layout.add (std::move (outputGroup));

    return layout;
}

//==============================================================================
// Factory programs
//==============================================================================
int ZTameAudioProcessor::getNumPrograms()
{
    return numFactoryPrograms;
}

int ZTameAudioProcessor::getCurrentProgram()
{
    return currentProgram.load();
}

void ZTameAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= numFactoryPrograms)
        return;

    const auto& pr = kFactoryPrograms[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (paramSensitivity, pr.sensitivity);
    set (paramRange,       pr.rangeDb);
    set (paramSelectivity, pr.selectivity);
    set (paramFocusLow,    pr.focusLow);
    set (paramFocusHigh,   pr.focusHigh);
    set (paramAttack,      pr.attackMs);
    set (paramRelease,     pr.releaseMs);
    set (paramResolution,  (float) pr.planIndex);

    // Global output controls (mix / listen / bypass / sidechain arming) are not
    // touched: a preset change must not silently re-route detection or mute the mix.
    currentProgram.store (index);
}

const juce::String ZTameAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < numFactoryPrograms)
        return kFactoryPrograms[index].name;

    return {};
}

//==============================================================================
ZTameAudioProcessor::Params ZTameAudioProcessor::readParams() const noexcept
{
    Params p;

    if (pSensitivity != nullptr) p.sensitivity = pSensitivity->load();
    if (pRange       != nullptr) p.rangeDb     = pRange->load();
    if (pSelectivity != nullptr) p.selectivity = pSelectivity->load();
    if (pFocusLow    != nullptr) p.focusLowHz  = pFocusLow->load();
    if (pFocusHigh   != nullptr) p.focusHighHz = pFocusHigh->load();
    if (pAttack      != nullptr) p.attackMs    = pAttack->load();
    if (pRelease     != nullptr) p.releaseMs   = pRelease->load();
    if (pListen      != nullptr) p.listening   = pListen->load() > 0.5f;
    if (pDryWet      != nullptr) p.mix         = juce::jlimit (0.0f, 1.0f, pDryWet->load() * 0.01f);
    if (pBypass      != nullptr) p.bypassed    = pBypass->load() > 0.5f;
    if (pSidechain   != nullptr) p.sidechainOn = pSidechain->load() > 0.5f;
    if (pStereoLink  != nullptr) p.stereoLink  = pStereoLink->load() > 0.5f;
    if (pResolution  != nullptr) p.planIndex   = pResolution->load() >= 0.5f ? 1 : 0;
    if (pProcessingMode != nullptr)
        p.processingMode = juce::jlimit (0, 3, (int) std::lround (pProcessingMode->load()));

    p.focusLowHz  = juce::jlimit (20.0f, 20000.0f, p.focusLowHz);
    p.focusHighHz = juce::jlimit (20.0f, 20000.0f, p.focusHighHz);
    if (p.focusHighHz < p.focusLowHz * 1.01f)
        p.focusHighHz = juce::jmin (20000.0f, p.focusLowHz * 1.01f);

    return p;
}

//==============================================================================
void ZTameAudioProcessor::rebuildBandPlan (int planIndex) noexcept
{
    auto& plan = bandPlan;
    plan.numBands = (planIndex >= 1) ? maxBands : 32;

    const int nb = plan.numBands;
    const double fs = juce::jmax (1.0, currentSampleRate);
    const double fMax = juce::jmin ((double) kMaxFreq, 0.45 * fs);
    const double minF = (double) kMinFreq;
    const double ratio = fMax / minF;

    std::array<double, maxBands + 1> edges {};
    edges[0] = minF;
    for (int i = 1; i <= nb; ++i)
        edges[(size_t) i] = minF * std::pow (ratio, (double) i / (double) nb);

    for (int k = 0; k < nb; ++k)
    {
        const double c  = std::sqrt (edges[(size_t) k] * edges[(size_t) (k + 1)]);
        const double w0 = juce::MathConstants<double>::twoPi * c / fs;

        plan.centerHz[(size_t) k] = (float) c;
        plan.cosW0[(size_t) k]    = (float) std::cos (w0);
        plan.alpha[(size_t) k]    = (float) (std::sin (w0) / (2.0 * (double) kBellQ));
    }

    // FFT bin → band (bins outside 20 Hz..fMax are not corrected).
    {
        const double binHz = fs / (double) analysisN;
        int band = 0;

        for (int k = 0; k < analysisBins; ++k)
        {
            const double f = (double) k * binHz;

            while (band < nb - 1 && f >= edges[(size_t) (band + 1)])
                ++band;

            plan.binToBand[(size_t) k] = (f >= minF && f <= fMax) ? band : -1;
        }
    }

    // Display bin → FFT bin edges (20 Hz..20 kHz log axis, matching the editor).
    {
        const int D = UiSnapshot::numDisplayBins;

        for (int d = 0; d <= D; ++d)
        {
            const float f = displayNormToFreq ((float) d / (float) D);
            const int bin = (int) std::llround ((double) f * (double) analysisN / fs);
            plan.displayLookup[(size_t) d] = juce::jlimit (1, analysisBins - 2, bin);
        }

        for (int d = 1; d <= D; ++d)
            plan.displayLookup[(size_t) d] = juce::jmax (plan.displayLookup[(size_t) d],
                                                         plan.displayLookup[(size_t) (d - 1)]);
    }
}

//==============================================================================
void ZTameAudioProcessor::resetChannel (ChannelState& s) noexcept
{
    for (int k = 0; k < maxBands; ++k)
    {
        s.bells[k].setIdentity();
        s.bells[k].x1 = s.bells[k].x2 = s.bells[k].y1 = s.bells[k].y2 = 0.0f;
        s.cutDb[k] = 0.0f;
        s.appliedCutDb[k] = 0.0f;
        s.seedPending[k] = false;
        s.bandLevel[k] = 0.0f;
    }

    std::fill (std::begin (s.inRing), std::end (s.inRing), 0.0f);
    std::fill (std::begin (s.fftData), std::end (s.fftData), 0.0f);
    std::fill (std::begin (s.mag), std::end (s.mag), 0.0f);
    std::fill (std::begin (s.holdMag), std::end (s.holdMag), 0.0f);
    s.ringPos = 0;
    s.anyCut = false;
    s.prepared = true;
}

void ZTameAudioProcessor::resetAllChannels() noexcept
{
    for (auto& ch : mainChannels)
        resetChannel (ch);

    for (auto& ch : sideChannels)
        resetChannel (ch);

    grAverageSmoothed = 0.0f;
    grPeakSmoothed = 0.0f;
    grAverageDb.store (0.0f);
    grPeakDb.store (0.0f);
}

//==============================================================================
void ZTameAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxSamplesPerBlock = juce::jmax (16, samplesPerBlock);

    numMainChannels = 0;
    if (auto* bus = getBus (true, 0))
        numMainChannels = juce::jlimit (0, 2, bus->getNumberOfChannels());

    const int wantedPlan = (pResolution != nullptr && pResolution->load() >= 0.5f) ? 1 : 0;
    rebuildBandPlan (wantedPlan);
    activePlanIndex = wantedPlan;

    resetAllChannels();

    samplesUntilHop = analysisHop;

    const float hopSeconds = (float) ((double) analysisHop / currentSampleRate);
    holdCoeff    = (float) std::exp (-(double) hopSeconds / (double) kHoldSeconds);
    attackCoeff  = frameCoeff (readParams().attackMs,  hopSeconds);
    releaseCoeff = frameCoeff (readParams().releaseMs, hopSeconds);

    numSideChannels = 0;
    sidechainSilentHops = 0;
    sidechainLiveInternal = false;
    sidechainLive.store (false);

    for (auto& slot : snapshotSlots)
        slot = UiSnapshot {};

    snapshotWriteIndex.store (0);
    snapshotReadyIndex.store (-1);
    snapshotReadIndex.store (-1);
    publishCountdown = juce::jmax (1, (int) std::lround (currentSampleRate * 0.023));

    outputLevelDb.store (-100.0f);
    processingLoadPercent.store (0.0f);

    const auto p = readParams();
    mixSmooth.reset (currentSampleRate, 0.02);
    mixSmooth.setCurrentAndTargetValue (p.mix);

    // The v1.2 engine is causal end-to-end: no delay is introduced on any path.
    setLatencySamples (0);
    prepared = true;
}

void ZTameAudioProcessor::releaseResources()
{
    prepared = false;
}

bool ZTameAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);

    // Main path must be mono or stereo and in-place (in == out).
    if (mainIn != mainOut)
        return false;

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    // Optional sidechain input bus.
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);

        if (sc != juce::AudioChannelSet::disabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    if (layouts.outputBuses.size() != 1)
        return false;

    return true;
}

//==============================================================================
void ZTameAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    processBlockInternal (buffer, false);
}

void ZTameAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Zero latency: host bypass is a plain passthrough (there is no delay to keep
    // delay compensation coherent, so the buffer must stay untouched).
    processBlockInternal (buffer, true);
}

//==============================================================================
void ZTameAudioProcessor::processBlockInternal (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || ! prepared || numMainChannels <= 0)
        return;

    // --- band plan follows the resolution parameter (no latency to re-report) ---
    const int wantedPlan = (pResolution != nullptr && pResolution->load() >= 0.5f) ? 1 : 0;
    if (wantedPlan != activePlanIndex)
    {
        rebuildBandPlan (wantedPlan);
        activePlanIndex = wantedPlan;
        resetAllChannels();
        samplesUntilHop = analysisHop;
    }

    auto mainInBuf  = getBusBuffer (buffer, true, 0);
    auto mainOutBuf = getBusBuffer (buffer, false, 0);

    const int numCh = juce::jlimit (0, 2, juce::jmin (numMainChannels,
                                                     mainInBuf.getNumChannels(),
                                                     mainOutBuf.getNumChannels()));
    if (numCh <= 0)
        return;

    const Params p = readParams();
    const int nb = bandPlan.numBands;

    // --- sidechain bus (detection source only) ---
    int numSc = 0;
    juce::AudioBuffer<float> scBuf;
    const bool scArmed = (getBusCount (true) > 1)
                          && p.sidechainOn
                          && getBus (true, 1) != nullptr
                          && getBus (true, 1)->isEnabled();
    if (scArmed)
    {
        scBuf = getBusBuffer (buffer, true, 1);
        numSc = juce::jlimit (0, 2, scBuf.getNumChannels());
    }
    numSideChannels = numSc;

    // --- bypass --------------------------------------------------------------
    // Zero-latency engine → bypass is a sample-exact passthrough and the engine
    // is parked entirely; it re-opens (with fresh state) on the falling edge.
    const bool bypassed = hostBypass || p.bypassed;

    if (bypassed)
    {
        wasBypassed = true;
        sidechainSilentHops = 0;
        sidechainLiveInternal = false;
        sidechainLive.store (false);
        mixSmooth.skip (numSamples);
        return;
    }

    if (wasBypassed)
    {
        resetAllChannels();
        samplesUntilHop = analysisHop;
        wasBypassed = false;
    }

    // --- processing mode -----------------------------------------------------
    const bool useMidSide  = (p.processingMode != modeStereo) && numCh > 1;
    const bool processMid  = (p.processingMode != modeSideOnly);
    const bool processSide = (p.processingMode != modeMidOnly);

    bool mainProcessed[2] = { true, numCh > 1 };
    if (useMidSide)
    {
        mainProcessed[0] = processMid;
        mainProcessed[1] = processSide;
    }

    bool scProcessed[2] = { true, true };
    if (useMidSide)
    {
        scProcessed[0] = processMid;
        scProcessed[1] = processSide;
    }

    mixSmooth.setTargetValue (p.mix);

    const double blockSeconds = (double) numSamples / juce::jmax (1.0, currentSampleRate);

    // RAII so that every early return out of this function is still accounted for.
    struct LoadMeter
    {
        ZTameAudioProcessor& owner;
        juce::int64 startTicks;
        double budget;

        ~LoadMeter()
        {
            const auto elapsed = juce::Time::highResolutionTicksToSeconds (
                                     juce::Time::getHighResolutionTicks() - startTicks);
            const auto percent = budget > 0.0 ? (float) (100.0 * elapsed / budget) : 0.0f;
            const auto smoothed = juce::jmax (percent, owner.processingLoadPercent.load() * 0.85f);
            owner.processingLoadPercent.store (juce::jlimit (0.0f, 400.0f, smoothed));
        }
    } loadMeter { *this, juce::Time::getHighResolutionTicks(), blockSeconds };

    // --- silence fast path ----------------------------------------------------
    // The correction only ever subtracts energy, so with a silent input the output
    // cannot change (above the -90 dBFS gate). Release the engine at hop rate,
    // decay the states to match the unprocessed silence, and leave the buffer as-is.
    const float inputPeak = mainInBuf.getMagnitude (0, numSamples);

    if (inputPeak <= juce::Decibels::decibelsToGain (kSilenceGateDb))
    {
        // The engine cannot change anything audible, but the buffer must still leave
        // as valid audio: sanitise non-finite samples even on this fast path (JUCE's
        // magnitude scan ignores NaN, so a NaN-only block reads as "silent").
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = mainOutBuf.getWritePointer (ch);

            for (int n = 0; n < numSamples; ++n)
                if (! std::isfinite (d[n]))
                    d[n] = 0.0f;
        }

        std::fill (combinedHold.begin(), combinedHold.end(), 0.0f);

        for (auto* channels : { &mainChannels, &sideChannels })
            for (auto& ch : *channels)
            {
                std::fill (std::begin (ch.holdMag), std::end (ch.holdMag), 0.0f);
                std::fill (std::begin (ch.mag), std::end (ch.mag), 0.0f);
                std::fill (std::begin (ch.bandLevel), std::end (ch.bandLevel), 0.0f);

                // The correction cascade would have decayed its own state while the
                // input was silent; approximate that so audio can return without a
                // stale-state click.
                for (int k = 0; k < nb; ++k)
                {
                    if (ch.appliedCutDb[k] <= 0.0f)
                        continue;

                    auto& bell = ch.bells[k];
                    const float disc = bell.a1 * bell.a1 - 4.0f * bell.a2;
                    float radius;

                    if (disc >= 0.0f)
                    {
                        const float r = std::sqrt (disc);
                        radius = juce::jmax (std::abs ((-bell.a1 + r) * 0.5f),
                                             std::abs ((-bell.a1 - r) * 0.5f));
                    }
                    else
                    {
                        radius = std::sqrt (juce::jmax (0.0f, bell.a2));
                    }

                    const float decay = (float) std::pow (juce::jlimit (0.0f, 1.0f, radius),
                                                          (double) numSamples);
                    bell.x1 = 0.0f;
                    bell.x2 = 0.0f;
                    bell.y1 *= decay;
                    bell.y2 *= decay;
                }
            }

        for (int n = 0; n < numSamples; ++n)
        {
            if (--samplesUntilHop <= 0)
            {
                samplesUntilHop = analysisHop;
                runHop (p, mainProcessed, scProcessed, scArmed);
            }

            if (uiActive.load() && --publishCountdown <= 0)
            {
                publishCountdown = juce::jmax (1, (int) std::lround (currentSampleRate * 0.023));
                publishSnapshot (p, false);
            }
        }

        mixSmooth.skip (numSamples);
        outputLevelDb.store (-100.0f);
        return;
    }

    // --- sample loop ----------------------------------------------------------
    float blockPeak = 0.0f;
    const bool listening = p.listening;

    for (int n = 0; n < numSamples; ++n)
    {
        float working[2] = { 0.0f, 0.0f };

        for (int ch = 0; ch < numCh; ++ch)
            working[ch] = sanitise (mainInBuf.getSample (ch, n));

        if (useMidSide)
        {
            const float m = 0.5f * (working[0] + working[1]);
            const float s = 0.5f * (working[0] - working[1]);
            working[0] = m;
            working[1] = s;
        }

        // Input history for the analysis window (never touched by the audio path
        // itself — the FFT reads only past samples).
        for (int ch = 0; ch < numCh; ++ch)
            if (mainProcessed[ch])
            {
                auto& state = mainChannels[(size_t) ch];
                state.inRing[(size_t) state.ringPos] = working[ch];
                state.ringPos = (state.ringPos + 1) % analysisRing;
            }

        if (scArmed && numSc > 0)
        {
            float sc0 = sanitise (scBuf.getSample (0, n));
            float sc1 = (numSc > 1) ? sanitise (scBuf.getSample (1, n)) : sc0;

            if (useMidSide && numSc > 1)
            {
                const float m = 0.5f * (sc0 + sc1);
                const float s = 0.5f * (sc0 - sc1);
                sc0 = scProcessed[0] ? m : 0.0f;
                sc1 = scProcessed[1] ? s : 0.0f;
            }

            if (scProcessed[0])
            {
                auto& state = sideChannels[0];
                state.inRing[(size_t) state.ringPos] = sc0;
                state.ringPos = (state.ringPos + 1) % analysisRing;
            }

            if (numSc > 1 && scProcessed[1])
            {
                auto& state = sideChannels[1];
                state.inRing[(size_t) state.ringPos] = sc1;
                state.ringPos = (state.ringPos + 1) % analysisRing;
            }
        }

        // --- hop: analysis + detection + gain trajectory -----------------------
        if (--samplesUntilHop <= 0)
        {
            samplesUntilHop = analysisHop;

            for (int ch = 0; ch < numCh; ++ch)
                if (mainProcessed[ch])
                    analyseChannel (mainChannels[(size_t) ch]);

            if (scArmed && numSc > 0)
            {
                if (scProcessed[0]) analyseChannel (sideChannels[0]);
                if (numSc > 1 && scProcessed[1]) analyseChannel (sideChannels[1]);
            }

            runHop (p, mainProcessed, scProcessed, scArmed);
        }

        const float mix = mixSmooth.getNextValue();

        // Correction: both channels run their cascades in the same band loop so the
        // two independent recurrences can be scheduled in parallel (ILP).
        const bool active0 = numCh > 0 && mainProcessed[0] && mainChannels[0].anyCut;
        const bool active1 = numCh > 1 && mainProcessed[1] && mainChannels[1].anyCut;

        float wet0 = working[0];
        float wet1 = (numCh > 1) ? working[1] : 0.0f;

        if (active0 || active1)
        {
            auto& s0 = mainChannels[0];
            auto& s1 = mainChannels[1];

            for (int k = 0; k < nb; ++k)
            {
                if (active0 && s0.appliedCutDb[k] > 0.0f)
                {
                    if (s0.seedPending[k])
                    {
                        s0.bells[k].seed (wet0);   // switch-on without a click
                        s0.seedPending[k] = false;
                    }

                    wet0 = s0.bells[k].process (wet0);
                }

                if (active1 && s1.appliedCutDb[k] > 0.0f)
                {
                    if (s1.seedPending[k])
                    {
                        s1.bells[k].seed (wet1);
                        s1.seedPending[k] = false;
                    }

                    wet1 = s1.bells[k].process (wet1);
                }
            }
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float wet = (ch == 0) ? wet0 : wet1;

            const float y = listening
                                // Removed material only, +6 dB monitor. delta = dry - wet
                                // is exact: the correction is a subtractive peaking cascade.
                                ? working[ch] + ((working[ch] - wet) * kListenMonitor - working[ch]) * mix
                                : working[ch] + (wet - working[ch]) * mix;
            working[ch] = y;
            blockPeak = juce::jmax (blockPeak, std::abs (y));
        }

        if (useMidSide)
        {
            const float l = working[0] + working[1];
            const float r = working[0] - working[1];
            working[0] = l;
            working[1] = r;
        }

        for (int ch = 0; ch < numCh; ++ch)
            mainOutBuf.setSample (ch, n, working[ch]);
    }

    // Output level meter: instant attack, 20 dB/s release.
    {
        const auto peakDb = juce::Decibels::gainToDecibels (blockPeak, -100.0f);
        const auto decayed = outputLevelDb.load() - (float) (20.0 * blockSeconds);
        outputLevelDb.store (juce::jmax (peakDb, decayed));
    }

    // Telemetry (UI only; skipped entirely when no editor is attached).
    // The countdown is in SAMPLES, so it is decremented by the block length —
    // decrementing once per block would delay the first frame by ~23 seconds.
    if (uiActive.load())
    {
        publishCountdown -= numSamples;

        if (publishCountdown <= 0)
        {
            publishCountdown = juce::jmax (1, (int) std::lround (currentSampleRate * 0.023));
            publishSnapshot (p, sidechainLive.load());
        }
    }
}

//==============================================================================
void ZTameAudioProcessor::analyseChannel (ChannelState& s) noexcept
{
    auto* fft = analysisFft.get();
    if (fft == nullptr)
        return;

    float* dst = s.fftData;

    // Windowed frame: the newest N samples, ending at the current write position.
    int src = (s.ringPos - analysisN + analysisRing) % analysisRing;
    for (int i = 0; i < analysisN; ++i)
    {
        dst[i] = s.inRing[(size_t) src] * window[(size_t) i];
        if (++src >= analysisRing)
            src = 0;
    }

    // JUCE's real-only transform needs 2N floats; the second half is workspace.
    std::fill (dst + analysisN, dst + 2 * analysisN, 0.0f);

    fft->performRealOnlyForwardTransform (dst, true);

    const float scale = 2.0f / ((float) analysisN * juce::jmax (1.0e-6f, windowGain));

    // Fresh per-band levels for the metering weights.
    for (int k = 0; k < maxBands; ++k)
        s.bandLevel[k] = 0.0f;

    for (int k = 0; k < analysisBins; ++k)
    {
        const float re = dst[2 * k];
        const float im = dst[2 * k + 1];
        const float m  = std::sqrt (re * re + im * im) * scale;

        s.mag[(size_t) k] = m;
        s.holdMag[(size_t) k] = juce::jmax (m, s.holdMag[(size_t) k] * holdCoeff);

        const int band = bandPlan.binToBand[(size_t) k];
        if (band >= 0)
            s.bandLevel[band] = juce::jmax (s.bandLevel[band], m);
    }
}

//==============================================================================
void ZTameAudioProcessor::computeReductionSpectrum (const float* holdMag,
                                                    std::vector<float>& out,
                                                    const Params& p) noexcept
{
    const int numBins = analysisBins;

    const float thresholdDb = detectorThresholdDb (p);
    const float rangeDb     = juce::jmax (0.0f, p.rangeDb);
    const float binHz       = (float) (currentSampleRate / (double) analysisN);

    // dB domain + prefix sums for O(1) local neighbourhood averages
    for (int k = 0; k < numBins; ++k)
        dbScratch[(size_t) k] = probeDb (holdMag[k], kSilenceFloorDb);

    prefixScratch[0] = 0.0f;
    for (int k = 0; k < numBins; ++k)
        prefixScratch[(size_t) (k + 1)] = prefixScratch[(size_t) k] + dbScratch[(size_t) k];

    out[0] = 0.0f;
    out[(size_t) (numBins - 1)] = 0.0f;

    for (int k = 1; k < numBins - 1; ++k)
    {
        const float binLevelDb = dbScratch[(size_t) k];

        if (binLevelDb <= kBandGateDb)
        {
            out[(size_t) k] = 0.0f;
            continue;
        }

        // Frequency-adaptive neighbourhood, skipping the bin's own skirt.
        const int r = neighbourhoodRadius (k, currentSampleRate, analysisN);

        const int loStart = juce::jmax (1, k - r);
        const int loEnd   = k - 2;
        const int hiStart = k + 2;
        const int hiEnd   = juce::jmin (numBins - 2, k + r);

        float sum = 0.0f;
        int   count = 0;

        if (loEnd >= loStart)
        {
            sum += prefixScratch[(size_t) (loEnd + 1)] - prefixScratch[(size_t) loStart];
            count += loEnd - loStart + 1;
        }
        if (hiEnd >= hiStart)
        {
            sum += prefixScratch[(size_t) (hiEnd + 1)] - prefixScratch[(size_t) hiStart];
            count += hiEnd - hiStart + 1;
        }

        const float baselineDb = (count > 0) ? (sum / (float) count) : binLevelDb;
        const float excessDb   = binLevelDb - baselineDb;

        // Tame the peak down to the trigger level, clamped to the depth control.
        float reduceDb = excessDb - thresholdDb;
        reduceDb = (reduceDb > 0.0f) ? juce::jmin (reduceDb, rangeDb) : 0.0f;

        // Focus band: suppression is only allowed inside the band (soft skirts).
        reduceDb *= bandProfile ((float) k * binHz, p.focusLowHz, p.focusHighHz);

        out[(size_t) k] = reduceDb;
    }

    // Light smoothing along frequency: avoids isolated single-bin notches
    // (metallic chirp) while leaving a resonance's own bins fully suppressed.
    std::copy (out.begin(), out.begin() + numBins, dbScratch.begin());
    for (int k = 2; k < numBins - 2; ++k)
    {
        out[(size_t) k] = 0.25f * dbScratch[(size_t) (k - 1)]
                        + 0.50f * dbScratch[(size_t) k]
                        + 0.25f * dbScratch[(size_t) (k + 1)];
    }
}

//==============================================================================
void ZTameAudioProcessor::runHop (const Params& p,
                                  const bool* channelProcessed,
                                  const bool* scProcessed,
                                  bool scArmed) noexcept
{
    const int nb = bandPlan.numBands;
    const float hopSeconds = (float) ((double) analysisHop / juce::jmax (1.0, currentSampleRate));

    attackCoeff  = frameCoeff (p.attackMs,  hopSeconds);
    releaseCoeff = frameCoeff (p.releaseMs, hopSeconds);
    holdCoeff    = (float) std::exp (-(double) hopSeconds / (double) kHoldSeconds);

    // --- sidechain liveness ---------------------------------------------------
    if (scArmed)
    {
        float scPeak = 0.0f;

        for (int i = 0; i < 2; ++i)
            if (scProcessed[i])
                for (int k = 0; k < analysisBins; ++k)
                    scPeak = juce::jmax (scPeak, sideChannels[(size_t) i].mag[(size_t) k]);

        const bool scHasSignal = scPeak > juce::Decibels::decibelsToGain (-80.0f);

        if (scHasSignal)
            sidechainSilentHops = 0;
        else
            sidechainSilentHops = juce::jmin (sidechainSilentHops + 1, 1000000);

        sidechainLiveInternal = sidechainSilentHops < 24;   // ≈ 0.25 s at 44.1 kHz
    }
    else
    {
        sidechainSilentHops = 0;
        sidechainLiveInternal = false;
    }

    sidechainLive.store (sidechainLiveInternal);

    const bool fromSidechain = scArmed && sidechainLiveInternal;
    const bool linked = p.stereoLink || ! (channelProcessed[0] && channelProcessed[1]);

    // --- gain trajectory + coefficient refresh --------------------------------
    auto applyTargets = [&] (ChannelState& s, const float* targets)
    {
        bool any = false;

        for (int k = 0; k < nb; ++k)
        {
            float c = s.cutDb[(size_t) k];
            const float target = juce::jlimit (0.0f, juce::jmax (0.0f, p.rangeDb), targets[k]);
            const float coeff = (target > c) ? attackCoeff : releaseCoeff;

            c += (target - c) * coeff;

            if (! std::isfinite (c) || c < kProcessFloorDb)
                c = 0.0f;

            s.cutDb[(size_t) k] = c;

            if (c == 0.0f)
            {
                if (s.appliedCutDb[(size_t) k] != 0.0f)
                {
                    s.bells[(size_t) k].setIdentity();
                    s.appliedCutDb[(size_t) k] = 0.0f;
                }
            }
            else if (std::abs (c - s.appliedCutDb[(size_t) k]) > 1.0e-3f)
            {
                s.bells[(size_t) k].setCut (c, bandPlan.cosW0[(size_t) k],
                                            bandPlan.alpha[(size_t) k]);

                if (s.appliedCutDb[(size_t) k] == 0.0f)
                    s.seedPending[(size_t) k] = true;   // click-free hand-off

                s.appliedCutDb[(size_t) k] = c;
            }

            any = any || (s.appliedCutDb[(size_t) k] > 0.0f);
        }

        s.anyCut = any;
    };

    auto bandTargetsFrom = [&] (const std::vector<float>& reduction, float* bandTargets)
    {
        for (int b = 0; b < nb; ++b)
            bandTargets[b] = 0.0f;

        for (int k = 1; k < analysisBins - 1; ++k)
        {
            const int band = bandPlan.binToBand[(size_t) k];
            if (band >= 0)
                bandTargets[band] = juce::jmax (bandTargets[band], reduction[(size_t) k]);
        }

        // Smooth across bands so a resonance does not open a single-band hole and
        // bell skirts and detector widths agree.
        float smoothed[maxBands];
        for (int b = 0; b < nb; ++b)
        {
            const float a = bandTargets[juce::jmax (0, b - 1)];
            const float c = bandTargets[juce::jmin (nb - 1, b + 1)];
            smoothed[b] = 0.25f * a + 0.5f * bandTargets[b] + 0.25f * c;
        }

        for (int b = 0; b < nb; ++b)
            bandTargets[b] = smoothed[b];
    };

    float zeroTargets[maxBands] {};

    if (linked)
    {
        const bool* srcProcessed = fromSidechain ? scProcessed : channelProcessed;

        std::fill (combinedHold.begin(), combinedHold.end(), 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (! srcProcessed[ch])
                continue;

            const auto& src = fromSidechain ? sideChannels[(size_t) ch] : mainChannels[(size_t) ch];

            for (int k = 0; k < analysisBins; ++k)
                combinedHold[(size_t) k] = juce::jmax (combinedHold[(size_t) k], src.holdMag[(size_t) k]);
        }

        computeReductionSpectrum (combinedHold.data(), detectionReduction, p);

        float bandTargets[maxBands];
        bandTargetsFrom (detectionReduction, bandTargets);

        for (int ch = 0; ch < 2; ++ch)
            applyTargets (mainChannels[(size_t) ch],
                          channelProcessed[ch] ? bandTargets : zeroTargets);
    }
    else
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            if (channelProcessed[ch])
            {
                const auto& src = fromSidechain ? sideChannels[(size_t) ch]
                                                : mainChannels[(size_t) ch];

                computeReductionSpectrum (src.holdMag, detectionReduction, p);

                float bandTargets[maxBands];
                bandTargetsFrom (detectionReduction, bandTargets);
                applyTargets (mainChannels[(size_t) ch], bandTargets);
            }
            else
            {
                applyTargets (mainChannels[(size_t) ch], zeroTargets);
            }
        }
    }

    // --- metering -------------------------------------------------------------
    float weightedSum = 0.0f, weightSum = 0.0f, peakCut = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        if (! channelProcessed[ch])
            continue;

        const auto& s = mainChannels[(size_t) ch];

        for (int k = 0; k < nb; ++k)
        {
            const float w = s.bandLevel[(size_t) k];
            weightedSum += s.cutDb[(size_t) k] * w;
            weightSum += w;
            peakCut = juce::jmax (peakCut, s.cutDb[(size_t) k]);
        }
    }

    const float avgCut = (weightSum > 1.0e-12f) ? (weightedSum / weightSum) : 0.0f;

    grAverageSmoothed += (avgCut - grAverageSmoothed) * 0.25f;
    grPeakSmoothed = juce::jmax (peakCut, grPeakSmoothed * 0.94f);
    grAverageDb.store (grAverageSmoothed);
    grPeakDb.store (grPeakSmoothed);
}

//==============================================================================
float ZTameAudioProcessor::detectorThresholdDb (const Params& p) const noexcept
{
    const float sens = juce::jlimit (0.0f, 1.0f, p.sensitivity * 0.01f);
    const float sel  = juce::jlimit (0.0f, 1.0f, p.selectivity * 0.01f);

    // 24 dB (very hard to trigger) down to 6 dB; selectivity adds an independent
    // ±6 dB axis so it is audibly a separate control from sensitivity.
    return juce::jlimit (3.0f, 30.0f, 24.0f - 18.0f * sens + (sel - 0.5f) * 12.0f);
}

int ZTameAudioProcessor::neighbourhoodRadius (int binIndex, double sampleRate,
                                              int fftSize) noexcept
{
    const auto freq = (float) ((double) binIndex * sampleRate / (double) fftSize);
    return 2 + (int) (std::sqrt (freq / 1000.0f) * 2.5f);
}

float ZTameAudioProcessor::bandProfile (float freqHz, float lowHz, float highHz) noexcept
{
    if (freqHz >= lowHz && freqHz <= highHz)
        return 1.0f;

    if (freqHz < lowHz)
    {
        const float w = juce::jmax (1.0f, lowHz * kSkirtRatio);
        return smoothStep ((freqHz - (lowHz - w)) / w);
    }

    const float w = juce::jmax (1.0f, highHz * kSkirtRatio);
    return smoothStep (((highHz + w) - freqHz) / w);
}

//==============================================================================
float ZTameAudioProcessor::freqToDisplayNorm (float freqHz) noexcept
{
    constexpr float lo = 20.0f, hi = 20000.0f;
    return juce::jlimit (0.0f, 1.0f,
                         std::log (juce::jmax (lo, freqHz) / lo) / std::log (hi / lo));
}

float ZTameAudioProcessor::displayNormToFreq (float norm) noexcept
{
    constexpr float lo = 20.0f, hi = 20000.0f;
    return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, norm));
}

//==============================================================================
void ZTameAudioProcessor::publishSnapshot (const Params& p, bool scLive) noexcept
{
    const int writeIdx = snapshotWriteIndex.load (std::memory_order_relaxed);

    if (writeIdx == snapshotReadIndex.load (std::memory_order_acquire))
        return;   // the editor is copying this slot — skip one update instead of tearing

    UiSnapshot& s = snapshotSlots[(size_t) writeIdx];

    const int D = UiSnapshot::numDisplayBins;
    const int numMain = juce::jmin (numMainChannels, 2);

    for (int d = 0; d < D; ++d)
    {
        const int b0 = bandPlan.displayLookup[(size_t) d];
        const int b1 = juce::jmax (b0 + 1, bandPlan.displayLookup[(size_t) (d + 1)]);

        float inMax = kSilenceFloorDb;
        float redMax = 0.0f;

        for (int k = b0; k < b1 && k < analysisBins; ++k)
        {
            for (int ch = 0; ch < numMain; ++ch)
                inMax = juce::jmax (inMax, probeDb (mainChannels[(size_t) ch].mag[(size_t) k],
                                                    kSilenceFloorDb));

            redMax = juce::jmax (redMax, detectionReduction[(size_t) k]);
        }

        displayIn[(size_t) d]  = inMax;
        displayRed[(size_t) d] = redMax;
        displayOut[(size_t) d] = inMax - redMax;
    }

    // Sidechain spectrum (shown only while the bus is live).
    if (scLive && numSideChannels > 0)
    {
        for (int d = 0; d < D; ++d)
        {
            const int b0 = bandPlan.displayLookup[(size_t) d];
            const int b1 = juce::jmax (b0 + 1, bandPlan.displayLookup[(size_t) (d + 1)]);

            float scMax = kSilenceFloorDb;

            for (int k = b0; k < b1 && k < analysisBins; ++k)
                for (int ch = 0; ch < numSideChannels; ++ch)
                    scMax = juce::jmax (scMax, probeDb (sideChannels[(size_t) ch].mag[(size_t) k],
                                                        kSilenceFloorDb));

            displaySide[(size_t) d] = scMax;
        }
    }
    else
    {
        std::copy (displayIn.begin(), displayIn.end(), displaySide.begin());
    }

    std::copy (displayIn.begin(),  displayIn.end(),  s.inputDb.begin());
    std::copy (displayOut.begin(), displayOut.end(), s.outputDb.begin());
    std::copy (displayRed.begin(), displayRed.end(), s.reductionDb.begin());
    std::copy (displaySide.begin(), displaySide.end(), s.sidechainDb.begin());

    int numPeaks = 0;

    for (int d = 1; d < D - 1 && numPeaks < UiSnapshot::maxPeaks; ++d)
    {
        const float r = displayRed[(size_t) d];

        if (r >= 1.5f && r >= displayRed[(size_t) (d - 1)] && r > displayRed[(size_t) (d + 1)])
        {
            s.peakFreqHz[(size_t) numPeaks]      = displayNormToFreq (((float) d + 0.5f) / (float) D);
            s.peakReductionDb[(size_t) numPeaks] = r;
            ++numPeaks;
        }
    }

    s.numPeaks           = numPeaks;
    s.grAverageDb        = grAverageSmoothed;
    s.grPeakDb           = grPeakSmoothed;
    s.thresholdDb        = detectorThresholdDb (p);
    s.bandCount          = bandPlan.numBands;
    s.sampleRate         = currentSampleRate;
    s.listenMode         = p.listening;
    s.bypassed           = p.bypassed;
    s.sidechainOn        = p.sidechainOn;
    s.sidechainConnected = isSidechainConnected();
    s.sidechainLive      = scLive;
    s.processingMode     = p.processingMode;
    s.outputLevelDb      = outputLevelDb.load();
    s.processingLoad     = processingLoadPercent.load();

    snapshotReadyIndex.store (writeIdx, std::memory_order_release);
    snapshotWriteIndex.store (1 - writeIdx, std::memory_order_relaxed);
}

bool ZTameAudioProcessor::getLatestSnapshot (UiSnapshot& dest) const
{
    const int idx = snapshotReadyIndex.load (std::memory_order_acquire);

    if (idx < 0)
        return false;

    snapshotReadIndex.store (idx, std::memory_order_release);
    dest = snapshotSlots[(size_t) idx];
    snapshotReadIndex.store (-1, std::memory_order_release);

    return true;
}

const float* ZTameAudioProcessor::getMagnitudeData() const noexcept
{
    uiCacheValid = getLatestSnapshot (uiCache);
    return uiCacheValid ? uiCache.inputDb.data() : nullptr;
}

const float* ZTameAudioProcessor::getSuppressionData() const noexcept
{
    // Reads the frame cached by getMagnitudeData(), so both arrays come from the
    // same analysis frame and the curve can never shear between them.
    return uiCacheValid ? uiCache.reductionDb.data() : nullptr;
}

bool ZTameAudioProcessor::isSidechainConnected() const
{
    if (getBusCount (true) <= 1)
        return false;

    if (auto* bus = getBus (true, 1))
        return bus->isEnabled();

    return false;
}

//==============================================================================
juce::AudioProcessorEditor* ZTameAudioProcessor::createEditor()
{
    return new ZTameAudioProcessorEditor (*this);
}

//==============================================================================
void ZTameAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", 3, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void ZTameAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr)
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZTameAudioProcessor();
}
