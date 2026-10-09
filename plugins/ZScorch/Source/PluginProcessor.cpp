#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>

//==============================================================================
ZScorchAudioProcessor::ZScorchAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      undoManager (std::make_unique<juce::UndoManager>()),
      apvts (*this, undoManager.get(), "ZScorchState", createParameterLayout())
{
    // Cache raw parameter pointers for audio-thread access
    liftParam                = apvts.getRawParameterValue ("lift");
    characterParam           = apvts.getRawParameterValue ("character");
    mixParam                 = apvts.getRawParameterValue ("mix");

    satDriveLowParam         = apvts.getRawParameterValue ("satDriveLow");
    satDriveLoMidParam       = apvts.getRawParameterValue ("satDriveLoMid");
    satDriveHiMidParam       = apvts.getRawParameterValue ("satDriveHiMid");
    satDriveHighParam        = apvts.getRawParameterValue ("satDriveHigh");
    satModeLowParam          = apvts.getRawParameterValue ("satModeLow");
    satModeLoMidParam        = apvts.getRawParameterValue ("satModeLoMid");
    satModeHiMidParam        = apvts.getRawParameterValue ("satModeHiMid");
    satModeHighParam         = apvts.getRawParameterValue ("satModeHigh");
    satHarmRedLowParam       = apvts.getRawParameterValue ("satHarmRedLow");
    satHarmRedLoMidParam     = apvts.getRawParameterValue ("satHarmRedLoMid");
    satHarmRedHiMidParam     = apvts.getRawParameterValue ("satHarmRedHiMid");
    satHarmRedHighParam      = apvts.getRawParameterValue ("satHarmRedHigh");

    tiltAmountParam          = apvts.getRawParameterValue ("tiltAmount");

    exciterAmountParam       = apvts.getRawParameterValue ("exciterAmount");
    exciterEnabledParam      = apvts.getRawParameterValue ("exciterEnabled");

    bandCountParam           = apvts.getRawParameterValue ("bandCount");
    bandGainLowParam         = apvts.getRawParameterValue ("bandGainLow");
    bandGainLoMidParam       = apvts.getRawParameterValue ("bandGainLoMid");
    bandGainHiMidParam       = apvts.getRawParameterValue ("bandGainHiMid");
    bandGainHighParam        = apvts.getRawParameterValue ("bandGainHigh");

    preEqLowShelfFreqParam   = apvts.getRawParameterValue ("preEqLowShelfFreq");
    preEqLowShelfGainParam   = apvts.getRawParameterValue ("preEqLowShelfGain");
    preEqBellFreqParam       = apvts.getRawParameterValue ("preEqBellFreq");
    preEqBellGainParam       = apvts.getRawParameterValue ("preEqBellGain");
    preEqBellQParam          = apvts.getRawParameterValue ("preEqBellQ");
    preEqHighShelfFreqParam  = apvts.getRawParameterValue ("preEqHighShelfFreq");
    preEqHighShelfGainParam  = apvts.getRawParameterValue ("preEqHighShelfGain");

    postEqLowShelfFreqParam  = apvts.getRawParameterValue ("postEqLowShelfFreq");
    postEqLowShelfGainParam  = apvts.getRawParameterValue ("postEqLowShelfGain");
    postEqBellFreqParam      = apvts.getRawParameterValue ("postEqBellFreq");
    postEqBellGainParam      = apvts.getRawParameterValue ("postEqBellGain");
    postEqBellQParam         = apvts.getRawParameterValue ("postEqBellQ");
    postEqHighShelfFreqParam = apvts.getRawParameterValue ("postEqHighShelfFreq");
    postEqHighShelfGainParam = apvts.getRawParameterValue ("postEqHighShelfGain");

    outputCeilingParam       = apvts.getRawParameterValue ("outputCeiling");
    oversample4xParam        = apvts.getRawParameterValue ("oversample4x");

    // Initialize factory presets
    initFactoryPresets();

    // Initialize licensing — ZScorch uses a 14-day trial with RSA validation
    ZLicenseManager::getInstance().initialize("ZSCORCH");
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZScorchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Macro controls
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lift", "Lift",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "character", "Character",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "mix", "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    // --- Saturator module — per-band ---
    auto makeSatParams = [&](const juce::String& suffix)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "satDrive" + suffix, "Sat Drive " + suffix,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "satMode" + suffix, "Sat Mode " + suffix,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            "satHarmRed" + suffix, "Sat Harm Reduction " + suffix,
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f));
    };
    makeSatParams ("Low");
    makeSatParams ("LoMid");
    makeSatParams ("HiMid");
    makeSatParams ("High");

    // --- Dynamic Spectral Tilt ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "tiltAmount", "Tilt Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    // --- Exciter (disabled by default) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "exciterAmount", "Exciter Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "exciterEnabled", "Exciter Enabled", false));

    // --- Multi-band ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bandCount", "Bands",
        juce::NormalisableRange<float> (0.0f, 3.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bandGainLow", "Band Gain Low",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bandGainLoMid", "Band Gain Lo-Mid",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bandGainHiMid", "Band Gain Hi-Mid",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "bandGainHigh", "Band Gain High",
        juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f));

    // --- Pre-Emphasis EQ (shapes signal before saturation) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqLowShelfFreq", "PreEQ Low Shelf Freq",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqLowShelfGain", "PreEQ Low Shelf Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqBellFreq", "PreEQ Bell Freq",
        juce::NormalisableRange<float> (200.0f, 8000.0f, 1.0f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqBellGain", "PreEQ Bell Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqBellQ", "PreEQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.1f), 0.707f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqHighShelfFreq", "PreEQ High Shelf Freq",
        juce::NormalisableRange<float> (2000.0f, 20000.0f, 1.0f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preEqHighShelfGain", "PreEQ High Shelf Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    // --- Post-Emphasis EQ (compensates after saturation) ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqLowShelfFreq", "PostEQ Low Shelf Freq",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f), 100.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqLowShelfGain", "PostEQ Low Shelf Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqBellFreq", "PostEQ Bell Freq",
        juce::NormalisableRange<float> (200.0f, 8000.0f, 1.0f), 1000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqBellGain", "PostEQ Bell Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqBellQ", "PostEQ Bell Q",
        juce::NormalisableRange<float> (0.1f, 10.0f, 0.1f), 0.707f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqHighShelfFreq", "PostEQ High Shelf Freq",
        juce::NormalisableRange<float> (2000.0f, 20000.0f, 1.0f), 8000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "postEqHighShelfGain", "PostEQ High Shelf Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    // --- Output ---
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "outputCeiling", "Output Ceiling",
        juce::NormalisableRange<float> (-12.0f, -0.1f, 0.1f), -0.3f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "oversample4x", "Oversample 4x",
        juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
// Helper: Update crossover filter coefficients based on band count and boundaries
void ZScorchAudioProcessor::updateCrossoverFilters (double sampleRate)
{
    constexpr float crossoverLowHz  = 250.0f;
    constexpr float crossoverHighHz = 1500.0f;
    constexpr float Q = 0.707f; // 2nd-order Butterworth → cascaded = LR4

    const int numBands = static_cast<int> (activeBandCount) + 1;
    const int numChannels = juce::jmin (getTotalNumOutputChannels(),
                                         getTotalNumInputChannels());

    juce::dsp::ProcessSpec spec { sampleRate,
                                   static_cast<juce::uint32> (currentSamplesPerBlock),
                                   static_cast<juce::uint32> (numChannels) };

    for (int b = 0; b < numBands; ++b)
    {
        auto& chain = bands[b];

        // Default: all-pass (no filtering) — reset to unity
        *chain.lpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 1000.0f, Q, 1.0f);
        *chain.lpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, 1000.0f, Q, 1.0f);
        *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 1000.0f, Q, 1.0f);
        *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, 1000.0f, Q, 1.0f);
    }

    if (numBands == 1)
        return; // Single band: no crossover needed

    // Band 0: Low-pass at crossoverLowHz (LR4 = 2 × 2nd-order Butterworth LPF)
    {
        auto& chain = bands[0];
        *chain.lpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
            sampleRate, crossoverLowHz, Q);
        *chain.lpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
            sampleRate, crossoverLowHz, Q);
    }

    if (numBands == 2)
    {
        // Band 1: High-pass at crossoverLowHz
        auto& chain = bands[1];
        *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
            sampleRate, crossoverLowHz, Q);
        *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
            sampleRate, crossoverLowHz, Q);
    }
    else if (numBands == 3)
    {
        // Band 1 (Mid): HP at 250Hz + LP at 1.5kHz
        {
            auto& chain = bands[1];
            *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverLowHz, Q);
            *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverLowHz, Q);
            *chain.lpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHighHz, Q);
            *chain.lpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHighHz, Q);
        }

        // Band 2 (High): HP at 1.5kHz
        {
            auto& chain = bands[2];
            *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHighHz, Q);
            *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHighHz, Q);
        }
    }
    else if (numBands == 4)
    {
        constexpr float crossoverHiMidHz = 6000.0f;

        // Band 1 (Lo-Mid): HP at 250Hz + LP at 1.5kHz
        {
            auto& chain = bands[1];
            *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverLowHz, Q);
            *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverLowHz, Q);
            *chain.lpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHighHz, Q);
            *chain.lpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHighHz, Q);
        }

        // Band 2 (Hi-Mid): HP at 1.5kHz + LP at 6kHz
        {
            auto& chain = bands[2];
            *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHighHz, Q);
            *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHighHz, Q);
            *chain.lpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHiMidHz, Q);
            *chain.lpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (
                sampleRate, crossoverHiMidHz, Q);
        }

        // Band 3 (High): HP at 6kHz
        {
            auto& chain = bands[3];
            *chain.hpFilter.stage1.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHiMidHz, Q);
            *chain.hpFilter.stage2.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
                sampleRate, crossoverHiMidHz, Q);
        }
    }
}

//==============================================================================
// Helper: Extract per-band control vector from FFT spectrum
ControlVector ZScorchAudioProcessor::extractBandCV (
    int bandIndex, const float* spectrum, int numBins, float binWidthHz) const
{
    ControlVector cv;
    if (numBins <= 0 || binWidthHz <= 0.0f)
        return cv;

    constexpr float crossoverLowHz  = 250.0f;
    constexpr float crossoverHighHz = 1500.0f;
    const int numBands = static_cast<int> (activeBandCount) + 1;

    // Determine bin range for this band
    int startBin = 0;
    int endBin = numBins;

    if (numBands == 2)
    {
        if (bandIndex == 0)      { startBin = 0; endBin = juce::roundToInt (crossoverLowHz / binWidthHz); }
        else if (bandIndex == 1) { startBin = juce::roundToInt (crossoverLowHz / binWidthHz); endBin = numBins; }
    }
    else if (numBands == 3)
    {
        if (bandIndex == 0)      { startBin = 0; endBin = juce::roundToInt (crossoverLowHz / binWidthHz); }
        else if (bandIndex == 1) { startBin = juce::roundToInt (crossoverLowHz / binWidthHz); endBin = juce::roundToInt (crossoverHighHz / binWidthHz); }
        else                     { startBin = juce::roundToInt (crossoverHighHz / binWidthHz); endBin = numBins; }
    }
    else if (numBands == 4)
    {
        constexpr float crossoverHiMidHz = 6000.0f;
        if (bandIndex == 0)      { startBin = 0; endBin = juce::roundToInt (crossoverLowHz / binWidthHz); }
        else if (bandIndex == 1) { startBin = juce::roundToInt (crossoverLowHz / binWidthHz); endBin = juce::roundToInt (crossoverHighHz / binWidthHz); }
        else if (bandIndex == 2) { startBin = juce::roundToInt (crossoverHighHz / binWidthHz); endBin = juce::roundToInt (crossoverHiMidHz / binWidthHz); }
        else                     { startBin = juce::roundToInt (crossoverHiMidHz / binWidthHz); endBin = numBins; }
    }

    startBin = juce::jlimit (0, numBins, startBin);
    endBin   = juce::jlimit (startBin + 1, numBins, endBin);

    // Compute band energy
    float bandEnergy = 0.0f;
    for (int i = startBin; i < endBin; ++i)
        bandEnergy += spectrum[i] * spectrum[i];
    bandEnergy /= static_cast<float> (endBin - startBin);

    // Total energy across all bins
    float totalEnergy = 0.0f;
    for (int i = 0; i < numBins; ++i)
        totalEnergy += spectrum[i] * spectrum[i];

    const float epsilon = 1e-10f;
    if (totalEnergy < epsilon)
    {
        cv.spectralFlatness = 1.0f;
        return cv;
    }

    // harmonicDensity: energy concentration in this band vs total
    cv.harmonicDensity = juce::jlimit (0.0f, 1.0f, bandEnergy / totalEnergy);

    // brightness: high-frequency ratio specific to this band
    cv.brightness = 0.0f;

    // spectralFlatness: approximate from band energy ratio
    cv.spectralFlatness = 1.0f - cv.harmonicDensity * 0.5f;

    // crestFactor and activity remain zero for band-specific (full-band analysis provides these)
    cv.crestFactor = 0.0f;
    cv.activity = bandEnergy / (totalEnergy + epsilon);

    return cv;
}

//==============================================================================
// Helper: Update Pre-Emphasis EQ coefficients (RBJ cookbook biquads)
void ZScorchAudioProcessor::updatePreEqFilters (double sampleRate)
{
    const float lowShelfFreq  = preEqLowShelfFreqParam->load();
    const float lowShelfGain  = preEqLowShelfGainParam->load();
    const float bellFreq      = preEqBellFreqParam->load();
    const float bellGain      = preEqBellGainParam->load();
    const float bellQ         = preEqBellQParam->load();
    const float highShelfFreq = preEqHighShelfFreqParam->load();
    const float highShelfGain = preEqHighShelfGainParam->load();

    // Avoid heap allocation on audio thread when nothing changed
    if (lowShelfFreq == m_cachedPreLowShelfFreq && lowShelfGain == m_cachedPreLowShelfGain
        && bellFreq == m_cachedPreBellFreq && bellGain == m_cachedPreBellGain
        && bellQ == m_cachedPreBellQ && highShelfFreq == m_cachedPreHighShelfFreq
        && highShelfGain == m_cachedPreHighShelfGain && sampleRate == m_cachedPreSampleRate)
        return;

    m_cachedPreLowShelfFreq  = lowShelfFreq;
    m_cachedPreLowShelfGain  = lowShelfGain;
    m_cachedPreBellFreq      = bellFreq;
    m_cachedPreBellGain      = bellGain;
    m_cachedPreBellQ         = bellQ;
    m_cachedPreHighShelfFreq = highShelfFreq;
    m_cachedPreHighShelfGain = highShelfGain;
    m_cachedPreSampleRate    = static_cast<float> (sampleRate);

    *preEqLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate, lowShelfFreq, 0.707f,
        juce::Decibels::decibelsToGain (lowShelfGain));
    *preEqBell.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sampleRate, bellFreq, bellQ,
        juce::Decibels::decibelsToGain (bellGain));
    *preEqHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, highShelfFreq, 0.707f,
        juce::Decibels::decibelsToGain (highShelfGain));
}

//==============================================================================
// Helper: Update Post-Emphasis EQ coefficients (RBJ cookbook biquads)
void ZScorchAudioProcessor::updatePostEqFilters (double sampleRate)
{
    const float lowShelfFreq  = postEqLowShelfFreqParam->load();
    const float lowShelfGain  = postEqLowShelfGainParam->load();
    const float bellFreq      = postEqBellFreqParam->load();
    const float bellGain      = postEqBellGainParam->load();
    const float bellQ         = postEqBellQParam->load();
    const float highShelfFreq = postEqHighShelfFreqParam->load();
    const float highShelfGain = postEqHighShelfGainParam->load();

    // Avoid heap allocation on audio thread when nothing changed
    if (lowShelfFreq == m_cachedPostLowShelfFreq && lowShelfGain == m_cachedPostLowShelfGain
        && bellFreq == m_cachedPostBellFreq && bellGain == m_cachedPostBellGain
        && bellQ == m_cachedPostBellQ && highShelfFreq == m_cachedPostHighShelfFreq
        && highShelfGain == m_cachedPostHighShelfGain && sampleRate == m_cachedPostSampleRate)
        return;

    m_cachedPostLowShelfFreq  = lowShelfFreq;
    m_cachedPostLowShelfGain  = lowShelfGain;
    m_cachedPostBellFreq      = bellFreq;
    m_cachedPostBellGain      = bellGain;
    m_cachedPostBellQ         = bellQ;
    m_cachedPostHighShelfFreq = highShelfFreq;
    m_cachedPostHighShelfGain = highShelfGain;
    m_cachedPostSampleRate    = static_cast<float> (sampleRate);

    *postEqLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate, lowShelfFreq, 0.707f,
        juce::Decibels::decibelsToGain (lowShelfGain));
    *postEqBell.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (
        sampleRate, bellFreq, bellQ,
        juce::Decibels::decibelsToGain (bellGain));
    *postEqHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, highShelfFreq, 0.707f,
        juce::Decibels::decibelsToGain (highShelfGain));
}

//==============================================================================
// Helper: Output ceiling limiter — transparent below the knee, C1-continuous
// quadratic knee up to the ceiling, flat above it.
//
// Voiced for musicality rather than textbook tanh: everything below 60% of the
// ceiling passes bit-exact (no constant colouration), the knee is derivative-
// continuous (no buzz), and the bound is exactly the ceiling (no overshoot).
void ZScorchAudioProcessor::applyOutputCeiling (
    juce::AudioBuffer<float>& buffer, float ceilingDb)
{
    ceilingDb = juce::jlimit (-12.0f, -0.1f, ceilingDb);
    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    const float kneeStart = 0.60f * ceiling;
    const float kneeEnd   = 1.40f * ceiling;          // slope reaches zero here
    const float bound     = 0.50f * (kneeStart + kneeEnd);  // == ceiling
    const float span      = kneeEnd - kneeStart;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float v = data[i];

            // Cheap NaN/Inf sanitation (NaN fails both comparisons).
            if (! (v > -1.0e30f && v < 1.0e30f))
                v = 0.0f;

            const float a = std::abs (v);
            if (a > kneeStart)
            {
                const float over = a - kneeStart;
                const float sign = (v < 0.0f) ? -1.0f : 1.0f;
                v = (over >= span) ? sign * bound
                                   : sign * (a - (over * over) / (2.0f * span));
            }

            data[i] = v;
        }
    }
}

//==============================================================================
void ZScorchAudioProcessor::prepareToPlay (double sampleRate,
                                                       int samplesPerBlock)
{
    currentSampleRate    = sampleRate;
    currentSamplesPerBlock = samplesPerBlock;

    const auto numChannels = static_cast<int> (
        juce::jmin (getTotalNumOutputChannels(), getTotalNumInputChannels()));

    // Allocate dry buffer for wet/dry mix
    dryBuffer.setSize (numChannels, samplesPerBlock, false, true);
    dryBuffer.clear();

    // Initialize shared AnalysisCore
    analysisCore.prepare (sampleRate, samplesPerBlock);

    // Initialize full-band ControlLayer
    controlLayer.prepare (sampleRate);

    // Initialize all 3 possible band chains
    for (int b = 0; b < BandChain::kMaxBands; ++b)
    {
        bands[b].prepare (sampleRate, samplesPerBlock, numChannels);
        bandBuffers[b].setSize (numChannels, samplesPerBlock, false, true);
        bandBuffers[b].clear();
    }

    // Setup crossover filters
    updateCrossoverFilters (sampleRate);

    // Initialize Pre/Post-EQ biquads
    juce::dsp::ProcessSpec eqSpec { sampleRate,
                                     static_cast<juce::uint32> (samplesPerBlock),
                                     static_cast<juce::uint32> (numChannels) };
    preEqLowShelf.prepare (eqSpec);
    preEqBell.prepare (eqSpec);
    preEqHighShelf.prepare (eqSpec);
    postEqLowShelf.prepare (eqSpec);
    postEqBell.prepare (eqSpec);
    postEqHighShelf.prepare (eqSpec);
    updatePreEqFilters (sampleRate);
    updatePostEqFilters (sampleRate);

    // Force 4x oversampling on all saturators if user selected it
    const bool use4x = oversample4xParam->load() > 0.5f;
    for (int b = 0; b < BandChain::kMaxBands; ++b)
        bands[b].saturator.setForce4xOversampling (use4x);

    // Report combined latency
    const int satLatency = bands[0].saturator.getLatencySamples();
    const int excLatency = bands[0].exciter.getLatencySamples();
    setLatencySamples (satLatency + excLatency);

    // Reset the master DC blocker and remember the HQ state for latency updates.
    for (auto& dc : masterDc)
        dc.reset();
    lastHQActive = use4x;

    controlCounter = 0;
}

void ZScorchAudioProcessor::releaseResources()
{
    analysisCore.reset();
    controlLayer.reset();
    for (int b = 0; b < BandChain::kMaxBands; ++b)
        bands[b].reset();
    for (int b = 0; b < BandChain::kMaxBands; ++b)
        bandBuffers[b].setSize (0, 0);
    dryBuffer.clear();

    preEqLowShelf.reset();
    preEqBell.reset();
    preEqHighShelf.reset();
    postEqLowShelf.reset();
    postEqBell.reset();
    postEqHighShelf.reset();
}

//==============================================================================
#ifndef JucePlugin_PreferredChannelConfigurations
bool ZScorchAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

//==============================================================================
void ZScorchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                      juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto blockStartTicks = juce::Time::getHighResolutionTicks();

    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const auto numChannels = juce::jmin (getTotalNumOutputChannels(),
                                         buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    // --- Read atomic parameter values once per block ---
    const float liftVal         = liftParam->load();
    const float characterVal    = characterParam->load();
    const float mixVal          = mixParam->load();

    // Per-band saturation params
    const float satDriveLowVal     = satDriveLowParam->load();
    const float satDriveLoMidVal   = satDriveLoMidParam->load();
    const float satDriveHiMidVal   = satDriveHiMidParam->load();
    const float satDriveHighVal    = satDriveHighParam->load();
    const float satModeLowVal      = satModeLowParam->load();
    const float satModeLoMidVal    = satModeLoMidParam->load();
    const float satModeHiMidVal    = satModeHiMidParam->load();
    const float satModeHighVal     = satModeHighParam->load();
    const float satHarmRedLowVal   = satHarmRedLowParam->load();
    const float satHarmRedLoMidVal = satHarmRedLoMidParam->load();
    const float satHarmRedHiMidVal = satHarmRedHiMidParam->load();
    const float satHarmRedHighVal  = satHarmRedHighParam->load();

    const float tiltVal         = tiltAmountParam->load();
    const float exciterAmtVal   = exciterAmountParam->load();
    const float exciterEnVal    = exciterEnabledParam->load();

    const int bandCountInt      = static_cast<int> (bandCountParam->load() + 0.25f);
    const float bandGainLowDb   = bandGainLowParam->load();
    const float bandGainLoMidDb = bandGainLoMidParam->load();
    const float bandGainHiMidDb = bandGainHiMidParam->load();
    const float bandGainHighDb  = bandGainHighParam->load();

    const float ceilingDb       = outputCeilingParam->load();
    const bool  use4xOS         = oversample4xParam->load() > 0.5f;

    // Determine active band mode
    BandCount newBandCount = BandCount::Single;
    if (bandCountInt == 1)       newBandCount = BandCount::Two;
    else if (bandCountInt == 2)  newBandCount = BandCount::Three;
    else if (bandCountInt >= 3)  newBandCount = BandCount::Four;

    const int numBands = static_cast<int> (newBandCount) + 1;

    // If band count changed, update crossover filters
    if (newBandCount != activeBandCount)
    {
        activeBandCount = newBandCount;
        updateCrossoverFilters (currentSampleRate);
    }

    // Update 4x oversampling flag on all saturators
    for (int b = 0; b < BandChain::kMaxBands; ++b)
        bands[b].saturator.setForce4xOversampling (use4xOS);

    // HQ mode changes the saturator latency — report it to the host.
    if (use4xOS != lastHQActive)
    {
        lastHQActive = use4xOS;
        setLatencySamples (bands[0].saturator.getLatencySamples()
                           + bands[0].exciter.getLatencySamples());
    }

    // --- NaN/Inf guard: sanitize input before processing ---
    // Two float comparisons per sample (NaN fails both) — vectorises far better
    // than std::isnan/std::isinf and keeps the guard cost near zero.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int s = 0; s < numSamples; ++s)
        {
            if (! (data[s] > -1.0e30f && data[s] < 1.0e30f))
                data[s] = 0.0f;
        }
    }

    // --- Save dry buffer for wet/dry mix (before any processing) ---
    dryBuffer.makeCopyOf (buffer, true);

    // --- Master bypass (UI button): pass input through untouched ---
    if (bypassEnabled.load (std::memory_order_relaxed))
    {
        buffer.makeCopyOf (dryBuffer, true);
        updateLevelMeters (dryBuffer, buffer, numChannels, numSamples);
        publishProcessingLoad (blockStartTicks, numSamples);
        return;
    }

    // --- Pre-Emphasis EQ (shape signal before saturation) ---
    updatePreEqFilters (currentSampleRate);
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        preEqLowShelf.process (ctx);
        preEqBell.process (ctx);
        preEqHighShelf.process (ctx);
    }

    // --- Control-rate analysis (every ~256 samples) ---
    controlCounter += numSamples;
    if (controlCounter >= kControlInterval)
    {
        controlCounter -= kControlInterval;

        analysisCore.process (buffer, numSamples, numChannels);
        const auto& fullCV = analysisCore.getControlVector();

        // Per-band sat drive/mode/harmRed arrays indexed by band
        const float satDrivePerBand[4] = { satDriveLowVal, satDriveLoMidVal,
                                           satDriveHiMidVal, satDriveHighVal };
        const float satModePerBand[4]  = { satModeLowVal, satModeLoMidVal,
                                           satModeHiMidVal, satModeHighVal };
        const float satHarmRedPerBand[4] = { satHarmRedLowVal, satHarmRedLoMidVal,
                                             satHarmRedHiMidVal, satHarmRedHighVal };
        const float bandGainPerBand[4] = { bandGainLowDb, bandGainLoMidDb,
                                           bandGainHiMidDb, bandGainHighDb };

        // Update control layers per band
        for (int b = 0; b < numBands; ++b)
        {
            // Use full-band CV for Single mode, band-specific for multi-band
            ControlVector cvToUse = fullCV;

            bands[b].controlLayer.update (
                cvToUse, liftVal, characterVal,
                satDrivePerBand[b], satModePerBand[b], satHarmRedPerBand[b],
                tiltVal, exciterAmtVal, exciterEnVal,
                bandGainPerBand[b], currentSampleRate);
        }

        // Update meter
        const float targetMeter = fullCV.activity * liftVal;
        meterLevel.store (targetMeter, std::memory_order_relaxed);
    }

    // --- Advance smoothed targets through this block ---
    if (numBands == 1)
    {
        // Single-band: use full-band ControlLayer for targeted tests compatibility
        controlLayer.update (
            analysisCore.getControlVector(),
            liftVal, characterVal,
            satDriveLowVal, satModeLowVal, satHarmRedLowVal,
            tiltVal, exciterAmtVal, exciterEnVal,
            0.0f, currentSampleRate);
    }

    for (int b = 0; b < numBands; ++b)
    {
        bands[b].controlLayer.skipSamples (numSamples);
    }

    // --- Per-band processing ---
    // Runs the Sat → Tilt → Exciter chain (plus band gain) for one buffer.
    auto processChain = [&] (int b, juce::AudioBuffer<float>& buf, float bandGainDb)
    {
        const auto targets = bands[b].controlLayer.getCurrentTargets();

        if (liftVal > 0.001f)
        {
            bands[b].saturator.process (buf, targets.satDrive,
                                        targets.satMode, targets.satHarmReduction);
            bands[b].tilt.process (buf, targets.tiltAmount);
        }

        if (exciterEnVal > 0.5f && liftVal > 0.001f)
        {
            bands[b].exciter.process (buf, targets.exciterAmount,
                                      targets.exciterEnabled);
        }

        if (std::abs (bandGainDb) > 0.01f)
            buf.applyGain (juce::Decibels::decibelsToGain (bandGainDb));
    };

    if (numBands == 1)
    {
        // Fast path (default Single-band mode): no crossover filters, no band
        // buffer copies — the chain runs in place on the main buffer.
        processChain (0, buffer, bandGainLowDb);
    }
    else
    {
        for (int b = 0; b < numBands; ++b)
        {
            auto& chain = bands[b];

            // Copy input to the band buffer, then split with the LR4 crossover.
            bandBuffers[b].makeCopyOf (buffer, true);
            {
                juce::dsp::AudioBlock<float> block (bandBuffers[b]);
                juce::dsp::ProcessContextReplacing<float> ctx (block);
                chain.hpFilter.process (ctx);
                chain.lpFilter.process (ctx);
            }

            const float bandGainDb = (b == 0) ? bandGainLowDb :
                                     (b == 1) ? bandGainLoMidDb :
                                     (b == 2) ? bandGainHiMidDb : bandGainHighDb;
            processChain (b, bandBuffers[b], bandGainDb);
        }

        // --- Sum band buffers into main output buffer ---
        buffer.clear();
        for (int b = 0; b < numBands; ++b)
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.addFrom (ch, 0, bandBuffers[b], ch, 0, numSamples);
    }

    // --- Post-Emphasis EQ (compensate after saturation) ---
    updatePostEqFilters (currentSampleRate);
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        postEqLowShelf.process (ctx);
        postEqBell.process (ctx);
        postEqHighShelf.process (ctx);
    }

    // --- Master DC blocker (asymmetric curves leave a whisper of DC behind) ---
    for (int ch = 0; ch < juce::jmin (numChannels, kMaxProcChannels); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& dc = masterDc[ch];
        for (int s = 0; s < numSamples; ++s)
            data[s] = dc.process (data[s]);
    }

    // --- Output ceiling limiter ---
    applyOutputCeiling (buffer, ceilingDb);

    // --- Dry/Wet mix ---
    if (mixVal < 0.999f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wetData = buffer.getWritePointer (ch);
            const auto* dryData = dryBuffer.getReadPointer (ch);
            for (int s = 0; s < numSamples; ++s)
            {
                wetData[s] = dryData[s] + mixVal * (wetData[s] - dryData[s]);
            }
        }
    }

    // Mute any unused output channels
    for (int ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // --- Output NaN/Inf guard ---
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int s = 0; s < numSamples; ++s)
        {
            if (! std::isfinite (data[s]))
                data[s] = 0.0f;
        }
    }

    // --- Level meters (normalized RMS, 0 dBFS ≈ 1.0, same scale as activity) ---
    updateLevelMeters (dryBuffer, buffer, numChannels, numSamples);
    publishProcessingLoad (blockStartTicks, numSamples);
}

//==============================================================================
//==============================================================================
void ZScorchAudioProcessor::publishProcessingLoad (juce::int64 blockStartTicks,
                                                  int numSamples) noexcept
{
    const auto elapsedTicks = juce::Time::getHighResolutionTicks() - blockStartTicks;
    const auto ticksPerSecond = static_cast<double> (juce::Time::getHighResolutionTicksPerSecond());
    const auto blockTicks = ticksPerSecond * static_cast<double> (numSamples)
                          / std::max (currentSampleRate, 1.0);
    if (blockTicks <= 0.0)
        return;

    const auto instantaneous = static_cast<float> (100.0 * static_cast<double> (elapsedTicks) / blockTicks);
    const auto previous = processingLoadPercent.load (std::memory_order_relaxed);
    processingLoadPercent.store (previous + (instantaneous - previous) * 0.08f,
                                 std::memory_order_relaxed);
}

void ZScorchAudioProcessor::updateLevelMeters (const juce::AudioBuffer<float>& input,
                                               const juce::AudioBuffer<float>& output,
                                               int numChannels, int numSamples)
{
    float inRms = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* d = input.getReadPointer (ch);
        for (int s = 0; s < numSamples; ++s)
            inRms += d[s] * d[s];
    }
    inRms = std::sqrt (inRms / static_cast<float> (numSamples * numChannels));

    float outRms = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* d = output.getReadPointer (ch);
        for (int s = 0; s < numSamples; ++s)
            outRms += d[s] * d[s];
    }
    outRms = std::sqrt (outRms / static_cast<float> (numSamples * numChannels));

    inputLevel.store (juce::jlimit (0.0f, 1.0f, inRms * 3.0f), std::memory_order_relaxed);
    outputLevel.store (juce::jlimit (0.0f, 1.0f, outRms * 3.0f), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* ZScorchAudioProcessor::createEditor()
{
    return new ZScorchAudioProcessorEditor (*this);
}

//==============================================================================
int ZScorchAudioProcessor::getNumPrograms()
{
    return static_cast<int> (factoryPresets.size());
}

int ZScorchAudioProcessor::getCurrentProgram()
{
    return currentPresetIndex;
}

void ZScorchAudioProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < static_cast<int> (factoryPresets.size()))
        applyPresetByIndex (index);
}

const juce::String ZScorchAudioProcessor::getProgramName (int index)
{
    if (index >= 0 && index < static_cast<int> (factoryPresets.size()))
        return factoryPresets[static_cast<size_t> (index)].name;
    return {};
}

void ZScorchAudioProcessor::applyPresetByIndex (int index)
{
    if (index < 0 || index >= static_cast<int> (factoryPresets.size()))
        return;

    const juce::String xmlStr = factoryPresets[static_cast<size_t> (index)].xmlState;
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (xmlStr));
    if (xml == nullptr)
        return;

    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->hasTagName ("PARAM"))
        {
            const juce::String id = child->getStringAttribute ("id");
            const float val = static_cast<float> (child->getDoubleAttribute ("value", 0.0));
            if (auto* param = apvts.getParameter (id))
            {
                // Convert to normalized 0..1 using parameter's range
                auto range = param->getNormalisableRange();
                const float norm = range.convertTo0to1 (val);
                param->setValueNotifyingHost (norm);
            }
        }
    }

    currentPresetIndex = index;
}

void ZScorchAudioProcessor::initFactoryPresets()
{
    factoryPresets.clear();

    auto makeXml = [] (const std::map<juce::String, double>& vals)
    {
        juce::String s = "<ZScorchState>";
        const std::vector<juce::String> keys = {
            "lift", "character", "mix",
            "satDriveLow", "satDriveLoMid", "satDriveHiMid", "satDriveHigh",
            "satModeLow", "satModeLoMid", "satModeHiMid", "satModeHigh",
            "satHarmRedLow", "satHarmRedLoMid", "satHarmRedHiMid", "satHarmRedHigh",
            "tiltAmount", "exciterAmount", "exciterEnabled",
            "bandCount",
            "bandGainLow", "bandGainLoMid", "bandGainHiMid", "bandGainHigh",
            "preEqLowShelfFreq", "preEqLowShelfGain",
            "preEqBellFreq", "preEqBellGain", "preEqBellQ",
            "preEqHighShelfFreq", "preEqHighShelfGain",
            "postEqLowShelfFreq", "postEqLowShelfGain",
            "postEqBellFreq", "postEqBellGain", "postEqBellQ",
            "postEqHighShelfFreq", "postEqHighShelfGain",
            "outputCeiling", "oversample4x"
        };
        for (const auto& k : keys)
        {
            double v = 0.0;
            auto it = vals.find (k);
            if (it != vals.end()) v = it->second;
            s += "<PARAM id=\"" + k + "\" value=\"" + juce::String (v, 6) + "\"/>";
        }
        s += "</ZScorchState>";
        return s;
    };

    // Defaults (from createParameterLayout)
    std::map<juce::String, double> defaults = {
        { "lift", 0.5 },
        { "character", 0.0 },
        { "mix", 1.0 },
        { "satDriveLow", 0.5 },
        { "satDriveLoMid", 0.5 },
        { "satDriveHiMid", 0.5 },
        { "satDriveHigh", 0.5 },
        { "satModeLow", 0.0 },
        { "satModeLoMid", 0.0 },
        { "satModeHiMid", 0.0 },
        { "satModeHigh", 0.0 },
        { "satHarmRedLow", 0.7 },
        { "satHarmRedLoMid", 0.7 },
        { "satHarmRedHiMid", 0.7 },
        { "satHarmRedHigh", 0.7 },
        { "tiltAmount", 0.5 },
        { "exciterAmount", 0.0 },
        { "exciterEnabled", 0.0 },
        { "bandCount", 0.0 },
        { "bandGainLow", 0.0 },
        { "bandGainLoMid", 0.0 },
        { "bandGainHiMid", 0.0 },
        { "bandGainHigh", 0.0 },
        { "preEqLowShelfFreq", 100.0 },
        { "preEqLowShelfGain", 0.0 },
        { "preEqBellFreq", 1000.0 },
        { "preEqBellGain", 0.0 },
        { "preEqBellQ", 0.707 },
        { "preEqHighShelfFreq", 8000.0 },
        { "preEqHighShelfGain", 0.0 },
        { "postEqLowShelfFreq", 100.0 },
        { "postEqLowShelfGain", 0.0 },
        { "postEqBellFreq", 1000.0 },
        { "postEqBellGain", 0.0 },
        { "postEqBellQ", 0.707 },
        { "postEqHighShelfFreq", 8000.0 },
        { "postEqHighShelfGain", 0.0 },
        { "outputCeiling", -0.3 },
        { "oversample4x", 0.0 }
    };

    //==========================================================================
    // ZScorch v2.0 factory bank — 60 presets in six families.
    //
    // Each preset is defined by what it does to the signal; no two entries share
    // the same drive/mode/tone combination. Saturation mode values are the
    // normalised positions of the six v2 curves (see ControlLayer):
    //   0.08 Tube · 0.25 Tape · 0.42 Transistor · 0.58 Germanium · 0.75 Diode
    //   0.92 Wavefold
    //==========================================================================
    constexpr double kTube       = 0.08;
    constexpr double kTape       = 0.25;
    constexpr double kTransistor = 0.42;
    constexpr double kGermanium  = 0.58;
    constexpr double kDiode      = 0.75;
    constexpr double kWavefold   = 0.92;

    auto preset = [&] (const char* name,
                       std::initializer_list<std::pair<const char*, double>> overrides)
    {
        auto v = defaults;
        for (const auto& o : overrides)
            v[o.first] = o.second;
        factoryPresets.push_back ({ juce::String (name), makeXml (v) });
    };

    // --- Family 1 · Clean lift & polish (1-8) --------------------------------
    preset ("Clean Lift",        { { "lift", 0.45 }, { "satDriveLow", 0.35 }, { "satHarmRedLow", 0.85 }, { "mix", 0.9 } });
    preset ("Studio Polish",     { { "lift", 0.55 }, { "satDriveLow", 0.30 }, { "satDriveHiMid", 0.35 }, { "satHarmRedLow", 0.85 }, { "satHarmRedHiMid", 0.9 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.25 }, { "character", 0.15 }, { "tiltAmount", 0.55 }, { "mix", 0.95 } });
    preset ("Velvet Gloss",      { { "lift", 0.5 }, { "satDriveLow", 0.25 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.9 }, { "tiltAmount", 0.55 }, { "character", 0.2 }, { "mix", 0.85 } });
    preset ("Air Lift",          { { "lift", 0.5 }, { "satDriveHigh", 0.2 }, { "satModeHigh", kTape }, { "satHarmRedHigh", 1.0 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.45 }, { "tiltAmount", 0.7 }, { "character", 0.35 } });
    preset ("Console Glue",      { { "lift", 0.5 }, { "satDriveLow", 0.4 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.75 }, { "mix", 0.95 } });
    preset ("Transparent Warmth",{ { "lift", 0.4 }, { "satDriveLow", 0.22 }, { "satHarmRedLow", 0.95 }, { "tiltAmount", 0.48 } });
    preset ("Mixbus Finish",     { { "lift", 0.5 }, { "bandCount", 1 }, { "satDriveLow", 0.30 }, { "satModeLow", kTube }, { "satDriveLoMid", 0.35 }, { "satModeLoMid", kTape }, { "satHarmRedLoMid", 0.8 }, { "outputCeiling", -1.0 }, { "mix", 0.95 } });
    preset ("Mastering Safe",    { { "lift", 0.35 }, { "satDriveLow", 0.2 }, { "satHarmRedLow", 0.9 }, { "outputCeiling", -1.2 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.15 } });

    // --- Family 2 · Warmth & vintage (9-20) ----------------------------------
    preset ("Warm Analog Glue",  { { "lift", 0.5 }, { "satDriveLow", 0.4 }, { "satModeLow", kTube }, { "character", -0.2 }, { "mix", 0.85 } });
    preset ("Vintage Tape",      { { "lift", 0.6 }, { "satDriveLow", 0.55 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.55 }, { "character", 0.1 }, { "tiltAmount", 0.45 } });
    preset ("Tape Sizzle",       { { "lift", 0.65 }, { "satDriveLow", 0.55 }, { "satModeLow", kTape }, { "character", 0.3 }, { "tiltAmount", 0.6 }, { "mix", 0.75 } });
    preset ("Reel Warmth",       { { "lift", 0.5 }, { "satDriveLow", 0.45 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.5 }, { "tiltAmount", 0.4 }, { "character", -0.15 }, { "mix", 0.8 } });
    preset ("Transformer Iron",  { { "lift", 0.6 }, { "satDriveLow", 0.6 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.6 }, { "mix", 0.8 } });
    preset ("Tube Preamp",       { { "lift", 0.55 }, { "satDriveLow", 0.5 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.7 }, { "character", 0.1 }, { "mix", 0.7 } });
    preset ("Tube Console Drive",{ { "lift", 0.7 }, { "satDriveLow", 0.65 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.6 }, { "tiltAmount", 0.5 }, { "mix", 0.6 } });
    preset ("Germanium Glow",    { { "lift", 0.7 }, { "satDriveLow", 0.7 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.35 }, { "mix", 0.55 } });
    preset ("Diode Warmth",      { { "lift", 0.6 }, { "satDriveLow", 0.55 }, { "satModeLow", kDiode }, { "satHarmRedLow", 0.5 }, { "mix", 0.7 } });
    preset ("Old Radio",         { { "lift", 0.6 }, { "bandCount", 2 }, { "satDriveLow", 0.5 }, { "satDriveLoMid", 0.6 }, { "satHarmRedLow", 0.2 }, { "satHarmRedLoMid", 0.2 }, { "tiltAmount", 0.3 }, { "character", -0.3 } });
    preset ("Cassette Deck",     { { "lift", 0.6 }, { "satDriveLow", 0.6 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.25 }, { "tiltAmount", 0.4 }, { "mix", 0.8 } });
    preset ("Vinyl Warmth",      { { "lift", 0.5 }, { "satDriveLow", 0.35 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.4 }, { "tiltAmount", 0.45 }, { "mix", 0.85 }, { "outputCeiling", -1.0 } });

    // --- Family 3 · Edge & crunch (21-32) ------------------------------------
    preset ("Transistor Edge",   { { "lift", 0.75 }, { "satDriveLow", 0.7 }, { "satModeLow", kTransistor }, { "character", 0.5 }, { "mix", 0.65 } });
    preset ("Aggressive Crunch", { { "lift", 0.9 }, { "satDriveLow", 0.85 }, { "satModeLow", kDiode }, { "character", 0.7 }, { "satHarmRedLow", 0.4 }, { "mix", 0.6 } });
    preset ("Tape Crunch",       { { "lift", 0.8 }, { "satDriveLow", 0.8 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.35 }, { "mix", 0.55 } });
    preset ("Guitar Bus Bite",   { { "lift", 0.8 }, { "bandCount", 1 }, { "satDriveLow", 0.7 }, { "satModeLow", kTransistor }, { "satDriveLoMid", 0.75 }, { "satModeLoMid", kTransistor }, { "satHarmRedLoMid", 0.45 }, { "tiltAmount", 0.55 }, { "character", 0.25 }, { "mix", 0.6 } });
    preset ("Drum Bus Punch",    { { "lift", 0.6 }, { "bandCount", 1 }, { "satDriveLow", 0.4 }, { "satModeLow", kTube }, { "satDriveLoMid", 0.2 }, { "bandGainLow", 2.0 }, { "outputCeiling", -0.5 } });
    preset ("Bass Grit",         { { "lift", 0.8 }, { "bandCount", 1 }, { "satDriveLow", 0.75 }, { "satModeLow", kDiode }, { "satDriveLoMid", 0.5 }, { "satModeLoMid", kDiode }, { "satHarmRedLow", 0.4 }, { "mix", 0.55 } });
    preset ("Snare Snap",        { { "lift", 0.7 }, { "bandCount", 2 }, { "satDriveHiMid", 0.8 }, { "satModeHiMid", kTransistor }, { "bandGainHiMid", 1.5 }, { "satHarmRedHiMid", 0.5 }, { "mix", 0.5 } });
    preset ("Vocal Heat",        { { "lift", 0.65 }, { "satDriveLow", 0.55 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.8 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.3 }, { "tiltAmount", 0.6 }, { "mix", 0.75 } });
    preset ("Rap Vocal Edge",    { { "lift", 0.7 }, { "satDriveLow", 0.6 }, { "satModeLow", kTransistor }, { "exciterEnabled", 1 }, { "exciterAmount", 0.4 }, { "tiltAmount", 0.65 }, { "mix", 0.7 } });
    preset ("Synth Drive",       { { "lift", 0.8 }, { "satDriveLow", 0.8 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.35 }, { "mix", 0.65 } });
    preset ("808 Smash",         { { "lift", 0.85 }, { "bandCount", 1 }, { "satDriveLow", 0.9 }, { "satModeLow", kDiode }, { "satHarmRedLow", 0.3 }, { "bandGainLow", 1.5 }, { "mix", 0.8 } });
    preset ("Lo-Fi Crush",       { { "lift", 0.85 }, { "bandCount", 3 }, { "satDriveLow", 0.8 }, { "satDriveLoMid", 0.8 }, { "satDriveHiMid", 0.8 }, { "satDriveHigh", 0.8 }, { "satHarmRedLow", 0.05 }, { "satHarmRedLoMid", 0.05 }, { "satHarmRedHiMid", 0.05 }, { "satHarmRedHigh", 0.05 }, { "tiltAmount", 0.25 }, { "mix", 0.8 } });

    // --- Family 4 · Multi-band shaping (33-40) -------------------------------
    preset ("Multi-Band Character", { { "lift", 0.7 }, { "bandCount", 2 }, { "satDriveLow", 0.5 }, { "satDriveLoMid", 0.5 }, { "satDriveHiMid", 0.5 }, { "satModeLow", kTransistor }, { "satModeLoMid", kTransistor }, { "satModeHiMid", kTransistor }, { "satHarmRedLoMid", 0.5 }, { "bandGainLoMid", 2.0 }, { "bandGainHiMid", 1.5 } });
    preset ("Low Warm High Air", { { "lift", 0.65 }, { "bandCount", 1 }, { "satDriveLow", 0.55 }, { "satModeLow", kTube }, { "satDriveHigh", 0.4 }, { "satModeHigh", kTape }, { "satHarmRedHigh", 0.8 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.25 } });
    preset ("Mid-Focus Drive",   { { "lift", 0.75 }, { "bandCount", 2 }, { "satDriveLoMid", 0.7 }, { "satModeLoMid", kTransistor }, { "bandGainLoMid", 2.0 }, { "satHarmRedLoMid", 0.5 }, { "mix", 0.75 } });
    preset ("Split Personality", { { "lift", 0.75 }, { "bandCount", 3 }, { "satDriveLow", 0.15 }, { "satModeLow", kTube }, { "satDriveLoMid", 0.35 }, { "satModeLoMid", kTape }, { "satDriveHiMid", 0.7 }, { "satModeHiMid", kTransistor }, { "satDriveHigh", 0.5 }, { "satModeHigh", kGermanium }, { "mix", 0.7 } });
    preset ("Tilted Bands",      { { "lift", 0.65 }, { "bandCount", 3 }, { "satDriveLow", 0.45 }, { "satDriveLoMid", 0.45 }, { "satDriveHiMid", 0.45 }, { "bandGainLow", 2.0 }, { "bandGainLoMid", -1.0 }, { "bandGainHiMid", -2.0 }, { "satHarmRedLoMid", 0.6 }, { "mix", 0.8 } });
    preset ("DJ Master",         { { "lift", 0.6 }, { "bandCount", 1 }, { "satDriveLow", 0.3 }, { "satModeLow", kTube }, { "satDriveHigh", 0.5 }, { "satModeHigh", kTape }, { "bandGainLow", 1.0 }, { "outputCeiling", -0.5 }, { "mix", 0.9 } });
    preset ("Podcast Voice",     { { "lift", 0.55 }, { "bandCount", 1 }, { "satDriveLow", 0.3 }, { "satModeLow", kTube }, { "satDriveHigh", 0.45 }, { "satModeHigh", kTape }, { "exciterEnabled", 1 }, { "exciterAmount", 0.2 }, { "tiltAmount", 0.55 }, { "outputCeiling", -1.0 } });
    preset ("Drum Stem Warmth",  { { "lift", 0.6 }, { "bandCount", 1 }, { "satDriveLow", 0.35 }, { "satModeLow", kTube }, { "satDriveLoMid", 0.5 }, { "satModeLoMid", kTape }, { "satHarmRedLoMid", 0.6 } });

    // --- Family 5 · Creative & character FX (41-50) --------------------------
    preset ("Wavefold Lead",     { { "lift", 0.75 }, { "satDriveLow", 0.7 }, { "satModeLow", kWavefold }, { "satHarmRedLow", 0.6 }, { "mix", 0.5 } });
    preset ("Fold Chaos",        { { "lift", 0.95 }, { "satDriveLow", 0.95 }, { "satModeLow", kWavefold }, { "satHarmRedLow", 0.5 }, { "mix", 0.6 } });
    preset ("Sine Fold Drift",   { { "lift", 0.65 }, { "satDriveLow", 0.6 }, { "satModeLow", kWavefold }, { "tiltAmount", 0.7 }, { "character", 0.4 }, { "mix", 0.55 } });
    preset ("Metal Bite",        { { "lift", 0.85 }, { "satDriveLow", 0.9 }, { "satModeLow", kGermanium }, { "tiltAmount", 0.75 }, { "character", 0.6 }, { "mix", 0.5 } });
    preset ("Destroyed Tape",    { { "lift", 0.85 }, { "satDriveLow", 0.85 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.1 }, { "tiltAmount", 0.3 }, { "mix", 0.8 } });
    preset ("Buzzsaw",           { { "lift", 0.9 }, { "satDriveLow", 0.9 }, { "satModeLow", kDiode }, { "satHarmRedLow", 0.15 }, { "mix", 0.5 } });
    preset ("Ambient Glow",      { { "lift", 0.5 }, { "satDriveLow", 0.35 }, { "satModeLow", kWavefold }, { "satHarmRedLow", 0.85 }, { "tiltAmount", 0.45 }, { "mix", 0.35 } });
    preset ("Cinematic Rumble",  { { "lift", 0.7 }, { "bandCount", 1 }, { "satDriveLow", 0.6 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.5 }, { "bandGainLow", 2.0 }, { "mix", 0.7 } });
    preset ("Dialog Whisper",    { { "lift", 0.55 }, { "satDriveLow", 0.45 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.9 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.35 }, { "tiltAmount", 0.55 }, { "mix", 0.6 } });
    preset ("Sub Destroyer",     { { "lift", 0.85 }, { "bandCount", 1 }, { "satDriveLow", 0.85 }, { "satModeLow", kDiode }, { "satHarmRedLow", 0.25 }, { "bandGainLow", 3.0 }, { "mix", 0.75 } });

    // --- Family 6 · Mastering-grade finishing (51-60) ------------------------
    preset ("Smooth Mastering Chain", { { "lift", 0.4 }, { "satDriveLow", 0.25 }, { "satModeLow", kTube }, { "character", -0.3 }, { "tiltAmount", 0.45 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.3 }, { "outputCeiling", -1.5 }, { "mix", 0.95 } });
    preset ("Broadcast Loud",    { { "lift", 0.7 }, { "satDriveLow", 0.5 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.5 }, { "outputCeiling", -1.0 } });
    preset ("Streaming Ready",   { { "lift", 0.5 }, { "satDriveLow", 0.35 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.7 }, { "outputCeiling", -1.0 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.2 } });
    preset ("Vinyl Master Cut",  { { "lift", 0.5 }, { "bandCount", 1 }, { "satDriveLow", 0.3 }, { "satModeLow", kTape }, { "satHarmRedLow", 0.45 }, { "outputCeiling", -1.5 } });
    preset ("Reference Null",    { { "lift", 1.0 }, { "satDriveLow", 0.0 }, { "satDriveLoMid", 0.0 }, { "satDriveHiMid", 0.0 }, { "satDriveHigh", 0.0 }, { "tiltAmount", 0.0 }, { "mix", 1.0 } });
    preset ("Gentle Five Percent", { { "lift", 0.3 }, { "satDriveLow", 0.12 }, { "satModeLow", kTube }, { "satHarmRedLow", 0.95 }, { "tiltAmount", 0.5 } });
    preset ("Parallel Crush",    { { "lift", 0.9 }, { "satDriveLow", 0.9 }, { "satModeLow", kDiode }, { "mix", 0.35 } });
    preset ("Width And Air",     { { "lift", 0.5 }, { "satDriveLow", 0.25 }, { "satModeLow", kTape }, { "tiltAmount", 0.7 }, { "character", 0.45 }, { "exciterEnabled", 1 }, { "exciterAmount", 0.35 }, { "mix", 0.85 } });
    preset ("Loud And Round",    { { "lift", 0.75 }, { "satDriveLow", 0.6 }, { "satModeLow", kGermanium }, { "satHarmRedLow", 0.4 }, { "outputCeiling", -0.5 }, { "mix", 0.9 } });
    preset ("Final Glue",        { { "lift", 0.6 }, { "bandCount", 1 }, { "satDriveLow", 0.3 }, { "satModeLow", kTube }, { "satDriveLoMid", 0.35 }, { "satModeLoMid", kTape }, { "satHarmRedLoMid", 0.8 }, { "outputCeiling", -1.0 }, { "tiltAmount", 0.5 }, { "mix", 0.95 } });
}

//==============================================================================
void ZScorchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    xml->setAttribute ("currentPresetIndex", currentPresetIndex);
    copyXmlToBinary (*xml, destData);
}

void ZScorchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr)
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        currentPresetIndex = xmlState->getIntAttribute ("currentPresetIndex", -1);
    }
}

//==============================================================================
bool ZScorchAudioProcessor::isLicensedForUse() const noexcept
{
    const auto state = ZLicenseManager::getInstance().getStatus();
    return state == Zarlino::Licensing::LicenseState::Licensed
        || state == Zarlino::Licensing::LicenseState::Trial;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZScorchAudioProcessor();
}
