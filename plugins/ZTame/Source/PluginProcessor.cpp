#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

//==============================================================================
namespace
{
    constexpr float kSilenceFloorDb = -140.0f;
    constexpr float kBinGateDb      = -90.0f;    // bins quieter than this are never touched
    constexpr float kSkirtRatio     = 0.2599f;   // 2^(1/3) - 1 → one-third-octave band skirts
    constexpr float kListenMonitor  = 2.0f;      // +6 dB on the removed-material audition
    constexpr float kHoldSeconds    = 0.080f;    // detector per-bin peak hold

    inline float sanitise (float v) noexcept
    {
        return std::isfinite (v) ? v : 0.0f;
    }

    inline float smoothStep (float t) noexcept
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return t * t * (3.0f - 2.0f * t);
    }

    // One-pole coefficient for a time constant expressed in *frames* (hop periods).
    inline float frameCoeff (float millis, float hopSeconds) noexcept
    {
        const float t = juce::jmax (0.0005f, millis * 0.001f);
        return 1.0f - std::exp (-hopSeconds / t);
    }
}

//==============================================================================
ZTameAudioProcessor::ZTameAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZTameState", createParameterLayout())
{
    // FFT engines
    fftMin = std::make_unique<juce::dsp::FFT> (minFftOrder);
    fftMax = std::make_unique<juce::dsp::FFT> (maxFftOrder);

    // sqrt(Hann) analysis + synthesis windows.
    // Hann is periodic (denominator N, not N-1) so that
    //     Hann[i] + Hann[i + N/2] == 1 exactly
    // and, with sqrt windows on both sides,
    //     w_a[i] * w_s[i] == Hann[i]
    // Therefore the overlap-add reconstructs with unity gain and zero ripple,
    // and no normalisation divisor is needed anywhere.
    for (int order = minFftOrder; order <= maxFftOrder; ++order)
    {
        const int n = 1 << order;
        auto& win = windows[(size_t) order];
        win.resize ((size_t) n);

        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto hann = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                    * (double) i / (double) n);
            win[(size_t) i] = (float) std::sqrt (juce::jmax (0.0, hann));
            sum += win[(size_t) i];
        }
        windowGains[(size_t) order] = (float) (sum / (double) n);
    }

    // Scratch buffers (sized for the largest supported frame)
    dbScratch.assign      (maxNumBins, 0.0f);
    prefixScratch.assign  (maxNumBins + 1, 0.0f);
    detectionMag.assign   (maxNumBins, 0.0f);
    sharedReduction.assign(maxNumBins, 0.0f);
    displayIn.assign      (UiSnapshot::numDisplayBins, -140.0f);
    displayOut.assign     (UiSnapshot::numDisplayBins, -140.0f);
    displayRed.assign     (UiSnapshot::numDisplayBins, 0.0f);
    displaySide.assign    (UiSnapshot::numDisplayBins, -140.0f);
    displayLookup.assign  (UiSnapshot::numDisplayBins + 1, 1);

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
    pMidSide     = parameters.getRawParameterValue (paramMidSide);

    startTimerHz (8);   // watches the resolution parameter (latency must be re-reported)
}

ZTameAudioProcessor::~ZTameAudioProcessor()
{
    stopTimer();
}

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
        paramResolution, "Resolution",
        StringArray { "Fast (1024)", "Precise (2048)" }, 1));
    layout.add (std::move (shapeGroup));

    auto outputGroup = std::make_unique<AudioProcessorParameterGroup> ("output", "Output", "|");
    outputGroup->addChild (std::make_unique<AudioParameterFloat> (
        paramDryWet, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramStereoLink, "Stereo Link", true));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramMidSide, "Mid/Side", false));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramListenMode, "Delta (Listen)", false));
    outputGroup->addChild (std::make_unique<AudioParameterBool> (
        paramBypass, "Bypass", false));
    layout.add (std::move (outputGroup));

    return layout;
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
    if (pMidSide     != nullptr) p.midSide     = pMidSide->load() > 0.5f;

    p.focusLowHz  = juce::jlimit (20.0f, 20000.0f, p.focusLowHz);
    p.focusHighHz = juce::jlimit (20.0f, 20000.0f, p.focusHighHz);
    if (p.focusHighHz < p.focusLowHz * 1.01f)
        p.focusHighHz = juce::jmin (20000.0f, p.focusLowHz * 1.01f);

    return p;
}

//==============================================================================
void ZTameAudioProcessor::initChannel (ChannelState& s, int /*fftOrder*/)
{
    s.inRing.assign    (ringSize, 0.0f);
    s.outAcc.assign    (ringSize, 0.0f);
    s.dryRing.assign   (ringSize, 0.0f);
    s.fftData.assign   ((size_t) 2 * maxFftSize, 0.0f);
    s.magnitude.assign (maxNumBins, 0.0f);
    s.holdMag.assign   (maxNumBins, 0.0f);
    s.reductionDb.assign (maxNumBins, 0.0f);
    s.targetGain.assign  (maxNumBins, 1.0f);
    s.gainSmooth.assign  (maxNumBins, 1.0f);
    s.prepared = true;
}

void ZTameAudioProcessor::resetChannel (ChannelState& s) noexcept
{
    // Only fills; never resizes — safe to call from the audio thread.
    std::fill (s.inRing.begin(),    s.inRing.end(),    0.0f);
    std::fill (s.outAcc.begin(),    s.outAcc.end(),    0.0f);
    std::fill (s.dryRing.begin(),   s.dryRing.end(),   0.0f);
    std::fill (s.fftData.begin(),   s.fftData.end(),   0.0f);
    std::fill (s.magnitude.begin(), s.magnitude.end(), 0.0f);
    std::fill (s.holdMag.begin(),   s.holdMag.end(),   0.0f);
    std::fill (s.reductionDb.begin(), s.reductionDb.end(), 0.0f);
    std::fill (s.targetGain.begin(),  s.targetGain.end(),  1.0f);
    std::fill (s.gainSmooth.begin(),  s.gainSmooth.end(),  1.0f);
}

void ZTameAudioProcessor::resetWetState (ChannelState& s) noexcept
{
    // Keep the delay lines (in/dry) running so the dry path stays continuous,
    // but empty the overlap-add accumulator and re-open every notch to unity.
    std::fill (s.outAcc.begin(),    s.outAcc.end(),    0.0f);
    std::fill (s.holdMag.begin(),   s.holdMag.end(),   0.0f);
    std::fill (s.reductionDb.begin(), s.reductionDb.end(), 0.0f);
    std::fill (s.targetGain.begin(),  s.targetGain.end(),  1.0f);
    std::fill (s.gainSmooth.begin(),  s.gainSmooth.end(),  1.0f);
}

//==============================================================================
void ZTameAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxSamplesPerBlock = juce::jmax (16, samplesPerBlock);

    numMainChannels = 0;
    if (auto* bus = getBus (true, 0))
        numMainChannels = juce::jlimit (0, 2, bus->getNumberOfChannels());

    const int wantedOrder = (pResolution != nullptr && pResolution->load() >= 1.0f)
                                ? maxFftOrder : minFftOrder;

    for (auto& ch : mainChannels) initChannel (ch, wantedOrder);
    for (auto& ch : sideChannels) initChannel (ch, wantedOrder);

    activeFftOrder.store (wantedOrder);
    activeFftSize.store (1 << wantedOrder);

    for (auto& slot : snapshotSlots)
        slot = UiSnapshot {};
    snapshotWriteIndex.store (0);
    snapshotReadyIndex.store (-1);
    snapshotReadIndex.store (-1);
    snapshotThrottle = 0;

    ringPos = 0;
    samplesSinceHop = 0;
    bypassDeep = false;
    bypassHoldSamples = 0;
    wasBypassed = false;
    silentSidechainHops = 0;
    sidechainInUse = false;
    numSideChannels = 0;
    sidechainLive.store (false);

    grAverageSmoothed = 0.0f;
    grPeakSmoothed = 0.0f;
    grAverageDb.store (0.0f);
    grPeakDb.store (0.0f);

    rebuildDisplayLookup (1 << wantedOrder);

    const auto p = readParams();
    mixSmooth.reset (currentSampleRate, 0.02);
    mixSmooth.setCurrentAndTargetValue (p.mix);

    setLatencySamples (1 << wantedOrder);
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
    // The plugin reports latency N, so it must keep delaying the signal even when
    // the host bypasses it — otherwise delay compensation goes out of sync.
    processBlockInternal (buffer, true);
}

void ZTameAudioProcessor::processBlockInternal (juce::AudioBuffer<float>& buffer, bool hostBypass)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || ! prepared || numMainChannels <= 0)
        return;

    // --- resolution change (parameter) ---
    // The engine switches immediately on the audio thread; the message-thread timer
    // re-reports the new latency to the host. Buffers are pre-allocated for the
    // largest frame, so this path performs no allocation.
    const int wantedOrder = (pResolution != nullptr && pResolution->load() >= 1.0f)
                                ? maxFftOrder : minFftOrder;

    if (wantedOrder != activeFftOrder.load())
    {
        activeFftOrder.store (wantedOrder);
        activeFftSize.store (1 << wantedOrder);

        for (auto& ch : mainChannels) resetChannel (ch);
        for (auto& ch : sideChannels) resetChannel (ch);

        rebuildDisplayLookup (1 << wantedOrder);
        ringPos = 0;
        samplesSinceHop = 0;
        bypassDeep = false;
        bypassHoldSamples = 0;
    }

    auto mainInBuf  = getBusBuffer (buffer, true, 0);
    auto mainOutBuf = getBusBuffer (buffer, false, 0);

    const int numCh = juce::jlimit (0, 2, juce::jmin (numMainChannels,
                                                     mainInBuf.getNumChannels(),
                                                     mainOutBuf.getNumChannels()));
    if (numCh <= 0)
        return;

    const Params p = readParams();
    const bool bypassed = hostBypass || p.bypassed;

    // --- sidechain bus ---
    int numSc = 0;
    juce::AudioBuffer<float> scBuf;
    const bool scBusAvailable = getBusCount (true) > 1;

    if (scBusAvailable && p.sidechainOn && getBus (true, 1) != nullptr
        && getBus (true, 1)->isEnabled())
    {
        scBuf = getBusBuffer (buffer, true, 1);
        numSc = juce::jlimit (0, 2, scBuf.getNumChannels());
    }
    numSideChannels = numSc;
    sidechainInUse = (numSc > 0) && sidechainLive.load();

    // --- bypass handling -----------------------------------------------------
    // Short bypass: keep the engine running so returning from bypass is seamless.
    // Long bypass: park the engine (saves CPU) and re-open all notches on resume,
    // so the hand-off back to the wet path is a transparent crossfade.
    if (bypassed)
    {
        bypassHoldSamples = juce::jmin (bypassHoldSamples + numSamples,
                                        (int) juce::jmax (1.0, currentSampleRate) * 4);
        if (! bypassDeep && bypassHoldSamples > (int) juce::jmax (1.0, currentSampleRate))
            bypassDeep = true;
    }
    else
    {
        if (bypassDeep)
        {
            for (auto& ch : mainChannels) resetWetState (ch);
            for (auto& ch : sideChannels) resetWetState (ch);
        }

        bypassDeep = false;
        bypassHoldSamples = 0;
    }
    wasBypassed = bypassed;

    const int N    = activeFftSize.load();
    const int hop  = N / 2;
    const bool listening = p.listening && ! bypassed;

    mixSmooth.setTargetValue (bypassed ? 1.0f : p.mix);

    // --- sample loop ---------------------------------------------------------
    for (int n = 0; n < numSamples; ++n)
    {
        // A frame is processed *before* the incoming sample is consumed: the frame
        // covers input samples [pos-N, pos), i.e. everything up to the sample we
        // are about to store. The output sample emitted this step is exactly
        // N samples old, which is the reported latency.
        if (samplesSinceHop >= hop)
        {
            samplesSinceHop = 0;

            if (! bypassDeep)
                runHop();
        }

        const float mix = mixSmooth.getNextValue();
        const int w = ringPos;
        const int rIdx = (w - N + ringSize) % ringSize;

        // Mid/Side mode processes the M and S signals as the two engine channels.
        // Encode: M = (L+R)/2, S = (L-R)/2.  Decode: L = M+S, R = M-S (unity round trip).
        const bool useMidSide = p.midSide && numCh > 1;

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

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& state = mainChannels[(size_t) ch];
            const float x = working[ch];

            state.inRing[(size_t) w] = x;

            const float wet = state.outAcc[(size_t) rIdx];
            state.outAcc[(size_t) rIdx] = 0.0f;

            // Dry delay line: write the current sample at the *current* slot and read
            // the slot from N samples ago, so the dry path is latency-aligned with the wet.
            const float dry = state.dryRing[(size_t) rIdx];
            state.dryRing[(size_t) w] = x;

            float y;

            if (bypassed)
            {
                y = dry;
            }
            else if (listening)
            {
                // Removed material only. delta = dry - wet is exact: resynthesis is
                // linear, and resynthesis of the full spectrum is the dry signal.
                const float delta = (dry - wet) * kListenMonitor;
                y = dry + (delta - dry) * mix;
            }
            else
            {
                y = dry + (wet - dry) * mix;
            }

            working[ch] = y;
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

        if (numSc > 0)
        {
            float sc0 = sanitise (scBuf.getSample (0, n));
            float sc1 = (numSc > 1) ? sanitise (scBuf.getSample (1, n)) : sc0;

            if (useMidSide && numSc > 1)
            {
                const float m = 0.5f * (sc0 + sc1);
                const float s = 0.5f * (sc0 - sc1);
                sc0 = m;
                sc1 = s;
            }

            sideChannels[0].inRing[(size_t) w] = sc0;
            if (numSc > 1)
                sideChannels[1].inRing[(size_t) w] = sc1;
        }

        ringPos = (ringPos + 1) % ringSize;
        ++samplesSinceHop;
    }
}

//==============================================================================
void ZTameAudioProcessor::runHop()
{
    if (! prepared)
        return;

    const int order   = activeFftOrder.load();
    const int N       = 1 << order;
    const int hop     = N / 2;
    const int numBins = N / 2 + 1;
    const Params p    = readParams();

    const int numMain = juce::jmin (numMainChannels, 2);

    // 1) analysis ------------------------------------------------------------
    for (int ch = 0; ch < numMain; ++ch)
        analyseChannel (mainChannels[(size_t) ch], order);

    for (int ch = 0; ch < numSideChannels; ++ch)
        analyseChannel (sideChannels[(size_t) ch], order);

    // Sidechain liveness: a connected-but-dead sidechain must not silently mute
    // the effect, so detection falls back to the main input until it carries signal.
    if (numSideChannels > 0)
    {
        float scPeak = 0.0f;
        for (int ch = 0; ch < numSideChannels; ++ch)
        {
            const auto& mag = sideChannels[(size_t) ch].magnitude;
            for (int k = 1; k < numBins - 1; ++k)
                scPeak = juce::jmax (scPeak, mag[(size_t) k]);
        }

        const bool scHasSignal = scPeak > juce::Decibels::decibelsToGain (-80.0f);

        if (scHasSignal)
            silentSidechainHops = 0;
        else
            silentSidechainHops = juce::jmin (silentSidechainHops + 1, 10000);

        sidechainLive.store (silentSidechainHops < 24);   // ≈ 0.5 s at 44.1 kHz
    }
    else
    {
        sidechainLive.store (false);
    }

    // 2) detection source ----------------------------------------------------
    const bool fromSidechain = (numSideChannels > 0) && sidechainLive.load();
    const bool linked = p.stereoLink || numMain <= 1;

    if (linked)
    {
        const int numSrc = fromSidechain ? numSideChannels : numMain;

        if (numSrc > 0)
        {
            const auto& first = fromSidechain ? sideChannels[0] : mainChannels[0];
            std::copy_n (first.holdMag.data(), (size_t) numBins, detectionMag.data());

            for (int ch = 1; ch < numSrc; ++ch)
            {
                const auto& src = fromSidechain ? sideChannels[(size_t) ch]
                                                : mainChannels[(size_t) ch];
                for (int k = 0; k < numBins; ++k)
                    detectionMag[(size_t) k] = juce::jmax (detectionMag[(size_t) k],
                                                           src.holdMag[(size_t) k]);
            }

            computeReductionSpectrum (detectionMag.data(), sharedReduction, p);

            for (int ch = 0; ch < numMain; ++ch)
                std::copy_n (sharedReduction.data(), (size_t) numBins,
                             mainChannels[(size_t) ch].reductionDb.data());
        }
    }
    else
    {
        for (int ch = 0; ch < numMain; ++ch)
        {
            const auto& src = (fromSidechain && ch < numSideChannels)
                                  ? sideChannels[(size_t) ch]
                                  : mainChannels[(size_t) ch];

            computeReductionSpectrum (src.holdMag.data(),
                                      mainChannels[(size_t) ch].reductionDb, p);
        }
    }

    // 3) resynthesis ---------------------------------------------------------
    const float hopSeconds  = (float) (hop / currentSampleRate);
    const float attackCoeff  = frameCoeff (p.attackMs,  hopSeconds);
    const float releaseCoeff = frameCoeff (p.releaseMs, hopSeconds);

    float avgAcc = 0.0f, peakAcc = 0.0f;

    for (int ch = 0; ch < numMain; ++ch)
    {
        float avg = 0.0f, peak = 0.0f;
        synthesiseChannel (mainChannels[(size_t) ch], order, p,
                           attackCoeff, releaseCoeff, avg, peak);
        avgAcc  = juce::jmax (avgAcc, avg);
        peakAcc = juce::jmax (peakAcc, peak);
    }

    // 4) metering ------------------------------------------------------------
    grAverageSmoothed += (avgAcc - grAverageSmoothed) * 0.25f;
    grPeakSmoothed = juce::jmax (peakAcc, grPeakSmoothed * 0.94f);
    grAverageDb.store (grAverageSmoothed);
    grPeakDb.store (grPeakSmoothed);

    // 5) telemetry (UI only; skipped entirely when no editor is attached) ----
    if (uiActive.load() && ++snapshotThrottle >= 2)
    {
        snapshotThrottle = 0;
        publishSnapshot (p, sidechainLive.load());
    }
}

//==============================================================================
void ZTameAudioProcessor::analyseChannel (ChannelState& s, int order) noexcept
{
    const int N = 1 << order;
    const int numBins = N / 2 + 1;

    auto* fft = (order == maxFftOrder) ? fftMax.get() : fftMin.get();
    if (fft == nullptr)
        return;

    const auto& win = windows[(size_t) order];
    float* dst = s.fftData.data();

    // Windowed frame: the newest N samples, ending at the current write position.
    int src = (ringPos - N + ringSize) % ringSize;
    for (int i = 0; i < N; ++i)
    {
        dst[i] = s.inRing[(size_t) src] * win[(size_t) i];
        if (++src >= ringSize)
            src = 0;
    }

    // JUCE's real-only transform needs 2N floats; the second half is workspace.
    std::fill (dst + N, dst + 2 * maxFftSize, 0.0f);

    fft->performRealOnlyForwardTransform (dst, true);

    // Magnitude in dBFS-referenced linear units (a full-scale sine reads ~0 dB).
    const float scale = 2.0f / ((float) N * juce::jmax (1.0e-6f, windowGains[(size_t) order]));
    const float hopSeconds = (float) (N / 2) / (float) juce::jmax (1.0, currentSampleRate);
    const float holdCoeff = std::exp (-hopSeconds / kHoldSeconds);

    float* mag  = s.magnitude.data();
    float* hold = s.holdMag.data();

    for (int k = 0; k < numBins; ++k)
    {
        const float re = dst[2 * k];
        const float im = dst[2 * k + 1];
        const float m  = std::sqrt (re * re + im * im) * scale;

        mag[(size_t) k] = m;
        hold[(size_t) k] = juce::jmax (m, hold[(size_t) k] * holdCoeff);
    }
}

//==============================================================================
void ZTameAudioProcessor::computeReductionSpectrum (const float* mag,
                                                    std::vector<float>& reductionOut,
                                                    const Params& p) noexcept
{
    const int order   = activeFftOrder.load();
    const int N       = 1 << order;
    const int numBins = N / 2 + 1;

    const float thresholdDb = detectorThresholdDb (p);
    const float rangeDb     = juce::jmax (0.0f, p.rangeDb);
    const float binHz       = (float) (currentSampleRate / (double) N);

    // dB domain + prefix sums for O(1) local neighbourhood averages
    dbScratch[0] = juce::Decibels::gainToDecibels (mag[0], kSilenceFloorDb);
    for (int k = 1; k < numBins; ++k)
        dbScratch[(size_t) k] = juce::Decibels::gainToDecibels (mag[(size_t) k], kSilenceFloorDb);

    prefixScratch[0] = 0.0f;
    for (int k = 0; k < numBins; ++k)
        prefixScratch[(size_t) (k + 1)] = prefixScratch[(size_t) k] + dbScratch[(size_t) k];

    reductionOut[0] = 0.0f;
    reductionOut[(size_t) (numBins - 1)] = 0.0f;

    for (int k = 1; k < numBins - 1; ++k)
    {
        const float binLevelDb = dbScratch[(size_t) k];

        if (binLevelDb <= kBinGateDb)
        {
            reductionOut[(size_t) k] = 0.0f;
            continue;
        }

        // Frequency-adaptive neighbourhood, skipping the bin's own skirt.
        const int r = neighbourhoodRadius (k, currentSampleRate, N);

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

        reductionOut[(size_t) k] = reduceDb;
    }

    // Light smoothing along frequency: avoids isolated single-bin notches
    // (metallic chirp) while leaving a resonance's own bins fully suppressed.
    std::copy (reductionOut.begin(), reductionOut.begin() + numBins, dbScratch.begin());
    for (int k = 2; k < numBins - 2; ++k)
    {
        reductionOut[(size_t) k] = 0.25f * dbScratch[(size_t) (k - 1)]
                                 + 0.50f * dbScratch[(size_t) k]
                                 + 0.25f * dbScratch[(size_t) (k + 1)];
    }
}

//==============================================================================
void ZTameAudioProcessor::synthesiseChannel (ChannelState& s, int order, const Params& p,
                                             float attackCoeff, float releaseCoeff,
                                             float& avgReductionDb,
                                             float& peakReductionDb) noexcept
{
    juce::ignoreUnused (p);

    const int N = 1 << order;
    const int numBins = N / 2 + 1;

    auto* fft = (order == maxFftOrder) ? fftMax.get() : fftMin.get();
    if (fft == nullptr)
        return;

    const auto& win = windows[(size_t) order];
    float* dst = s.fftData.data();

    const float* reduction = s.reductionDb.data();
    float* gainSmooth = s.gainSmooth.data();
    const float* mag = s.magnitude.data();

    double weightedSum = 0.0;
    double weightSum = 0.0;
    float peakDb = 0.0f;

    for (int k = 0; k < numBins; ++k)
    {
        const float targetDb = reduction[(size_t) k];
        const float targetGain = (targetDb > 0.0005f)
                                     ? juce::Decibels::decibelsToGain (-targetDb)
                                     : 1.0f;

        float g = gainSmooth[(size_t) k];

        const float coeff = (targetGain < g) ? attackCoeff : releaseCoeff;
        g += (targetGain - g) * coeff;

        if (! std::isfinite (g))
            g = 1.0f;

        g = juce::jlimit (0.0f, 1.0f, g);
        gainSmooth[(size_t) k] = g;

        if (g < 0.99995f)
        {
            const float appliedDb = juce::jlimit (0.0f, 60.0f,
                                                  -juce::Decibels::gainToDecibels (g, -60.0f));
            const float weight = mag[(size_t) k];
            weightedSum += (double) appliedDb * (double) weight;
            weightSum   += (double) weight;
            peakDb = juce::jmax (peakDb, appliedDb);
        }

        // Gains apply to the packed real-FFT layout: bin k lives at (2k, 2k+1).
        dst[2 * k]     *= g;
        dst[2 * k + 1] *= g;
    }

    fft->performRealOnlyInverseTransform (dst);

    // Overlap-add with the synthesis window. No normalisation factor: the window
    // pair is sqrt(Hann) which sums to exactly 1 across the two overlapping frames.
    int accPos = (ringPos - N + ringSize) % ringSize;
    for (int i = 0; i < N; ++i)
    {
        s.outAcc[(size_t) accPos] += dst[i] * win[(size_t) i];
        if (++accPos >= ringSize)
            accPos = 0;
    }

    avgReductionDb  = (weightSum > 0.0) ? (float) (weightedSum / weightSum) : 0.0f;
    peakReductionDb = peakDb;
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

void ZTameAudioProcessor::rebuildDisplayLookup (int fftSize) noexcept
{
    const int numBins = fftSize / 2 + 1;
    const int D = UiSnapshot::numDisplayBins;

    for (int d = 0; d <= D; ++d)
    {
        const float f = displayNormToFreq ((float) d / (float) D);
        const int bin = (int) std::llround ((double) f * (double) fftSize
                                            / juce::jmax (1.0, currentSampleRate));
        displayLookup[(size_t) d] = juce::jlimit (1, numBins - 2, bin);
    }

    for (int d = 1; d <= D; ++d)
        displayLookup[(size_t) d] = juce::jmax (displayLookup[(size_t) d],
                                                displayLookup[(size_t) (d - 1)]);
}

//==============================================================================
void ZTameAudioProcessor::publishSnapshot (const Params& p, bool scLive) noexcept
{
    const int writeIdx = snapshotWriteIndex.load (std::memory_order_relaxed);

    if (writeIdx == snapshotReadIndex.load (std::memory_order_acquire))
        return;   // the editor is copying this slot — skip one update instead of tearing

    UiSnapshot& s = snapshotSlots[(size_t) writeIdx];

    const int order   = activeFftOrder.load();
    const int N       = 1 << order;
    const int numBins = N / 2 + 1;
    const int D       = UiSnapshot::numDisplayBins;
    const int numMain = juce::jmin (numMainChannels, 2);

    // Applied reduction (dB) per FFT bin, from the (linked) first channel.
    for (int k = 0; k < numBins; ++k)
        dbScratch[(size_t) k] = juce::jlimit (0.0f, 60.0f,
            -juce::Decibels::gainToDecibels (mainChannels[0].gainSmooth[(size_t) k], -60.0f));

    for (int d = 0; d < D; ++d)
    {
        const int b0 = displayLookup[(size_t) d];
        const int b1 = juce::jmax (b0 + 1, displayLookup[(size_t) (d + 1)]);

        float inMax = kSilenceFloorDb, redMax = 0.0f, scMax = kSilenceFloorDb;

        for (int k = b0; k < b1 && k < numBins; ++k)
        {
            for (int ch = 0; ch < numMain; ++ch)
                inMax = juce::jmax (inMax, juce::Decibels::gainToDecibels (
                                               mainChannels[(size_t) ch].magnitude[(size_t) k],
                                               kSilenceFloorDb));

            redMax = juce::jmax (redMax, dbScratch[(size_t) k]);

            if (scLive && numSideChannels > 0)
                scMax = juce::jmax (scMax, juce::Decibels::gainToDecibels (
                                               sideChannels[0].magnitude[(size_t) k],
                                               kSilenceFloorDb));
        }

        displayIn[(size_t) d]  = inMax;
        displayRed[(size_t) d] = redMax;
        displayOut[(size_t) d] = inMax - redMax;
        displaySide[(size_t) d] = (scLive && numSideChannels > 0) ? scMax : inMax;
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
            s.peakFreqHz[(size_t) numPeaks] = displayNormToFreq (((float) d + 0.5f) / (float) D);
            s.peakReductionDb[(size_t) numPeaks] = r;
            ++numPeaks;
        }
    }

    s.numPeaks           = numPeaks;
    s.grAverageDb        = grAverageSmoothed;
    s.grPeakDb           = grPeakSmoothed;
    s.thresholdDb        = detectorThresholdDb (p);
    s.fftSize            = N;
    s.sampleRate         = currentSampleRate;
    s.listenMode         = p.listening;
    s.bypassed           = p.bypassed;
    s.sidechainOn        = p.sidechainOn;
    s.sidechainConnected = isSidechainConnected();
    s.sidechainLive      = scLive;
    s.midSide            = p.midSide;

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

bool ZTameAudioProcessor::isSidechainConnected() const
{
    if (getBusCount (true) <= 1)
        return false;

    if (auto* bus = getBus (true, 1))
        return bus->isEnabled();

    return false;
}

//==============================================================================
void ZTameAudioProcessor::timerCallback()
{
    if (pResolution == nullptr)
        return;

    const int wanted = (pResolution->load() >= 1.0f) ? maxFftOrder : minFftOrder;
    const int wantedLatency = 1 << wanted;

    if (getLatencySamples() != wantedLatency)
        setLatencySamples (wantedLatency);
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
    state.setProperty ("stateVersion", 2, nullptr);

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
