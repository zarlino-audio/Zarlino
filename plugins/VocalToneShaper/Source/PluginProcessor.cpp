#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr const char* inputTrimId = "global.input_trim";
    constexpr const char* outputTrimId = "global.output_trim";
    constexpr const char* outputCeilingId = "global.output_ceiling";
    constexpr const char* linearPhaseEqId = "global.linear_phase_eq";
    constexpr const char* eqCompOrderId = "global.routing.eq_comp_order";
    constexpr const char* learnedMakeupId = "global.auto_gain.learned_makeup_db";

    constexpr const char* stageABypassId = "stageA.bypass";
    constexpr const char* stageAMixId = "stageA.mix";
    constexpr const char* hpfEnabledId = "stageA.hpf.enabled";
    constexpr const char* hpfFreqId = "stageA.hpf.freq_hz";
    constexpr const char* lowMudFreqId = "stageA.band_low_mud.freq_hz";
    constexpr const char* lowMudGainId = "stageA.band_low_mud.gain_db";
    constexpr const char* lowMudQId = "stageA.band_low_mud.q";
    constexpr const char* moodFreqId = "stageA.band_mood.freq_hz";
    constexpr const char* moodGainId = "stageA.band_mood.gain_db";
    constexpr const char* moodQId = "stageA.band_mood.q";
    constexpr const char* presenceFreqId = "stageA.band_presence_cut.freq_hz";
    constexpr const char* presenceGainId = "stageA.band_presence_cut.gain_db";
    constexpr const char* presenceQId = "stageA.band_presence_cut.q";
    constexpr const char* airFreqId = "stageA.air_shelf.freq_hz";
    constexpr const char* airGainId = "stageA.air_shelf.gain_db";
    constexpr const char* airQId = "stageA.air_shelf.q";
    constexpr const char* driveId = "stageA.drive";

    constexpr const char* stageBBypassId = "stageB.bypass";
    constexpr const char* stageBMixId = "stageB.mix";
    constexpr const char* stageBInputId = "stageB.input_gain_db";
    constexpr const char* stageBOutputId = "stageB.output_gain_db";
    constexpr const char* stageBAttackId = "stageB.attack_ms";
    constexpr const char* stageBReleaseId = "stageB.release_ms";
    constexpr const char* stageBRatioId = "stageB.ratio";

    constexpr const char* stageCBypassId = "stageC.bypass";
    constexpr const char* stageCMixId = "stageC.mix";
    constexpr const char* stageCPhaseId = "stageC.phase_mode";
    constexpr const char* stageCSubRolloffId = "stageC.sub_rolloff.enabled";
    constexpr const char* stageCLowThresholdId = "stageC.band2.threshold_db";
    constexpr const char* stageCBoxThresholdId = "stageC.band3.threshold_db";
    constexpr const char* stageCMidThresholdId = "stageC.band4.threshold_db";
    constexpr const char* stageCHighMidThresholdId = "stageC.band5.threshold_db";
    constexpr const char* stageCHighThresholdId = "stageC.band6.threshold_db";

    constexpr const char* stageDBypassId = "stageD.bypass";
    constexpr const char* stageDMixId = "stageD.mix";
    constexpr const char* peakReductionId = "stageD.peak_reduction";
    constexpr const char* gainMakeupId = "stageD.gain_makeup_db";
    constexpr const char* stageDModeId = "stageD.mode";

    constexpr const char* stageEBypassId = "stageE.bypass";
    constexpr const char* stageEMixId = "stageE.mix";
    constexpr const char* midAirId = "stageE.mid_air_amount";
    constexpr const char* highAirId = "stageE.high_air_amount";
    constexpr const char* trimCompensateId = "stageE.trim_compensate";

    constexpr const char* stageFBypassId = "stageF.bypass";
    constexpr const char* stageFThresholdId = "stageF.threshold_db";
    constexpr const char* stageFFrequencyId = "stageF.frequency_hz";
    constexpr const char* stageFRangeId = "stageF.range_db";
    constexpr const char* stageFModeId = "stageF.mode";
    constexpr const char* stageFListenId = "stageF.listen_sidechain";

    float readParam (const juce::AudioProcessorValueTreeState& state, const char* id)
    {
        if (auto* value = state.getRawParameterValue (id))
            return value->load();

        jassertfalse;
        return 0.0f;
    }

    std::unique_ptr<juce::AudioParameterFloat> floatParam (const char* id, const char* name,
                                                           float min, float max, float def,
                                                           float step = 0.01f)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name,
            juce::NormalisableRange<float> (min, max, step), def);
    }

    std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const char* name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (id, name, def);
    }

    std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const char* name,
                                                             juce::StringArray choices, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (id, name, choices, def);
    }
}

VocalToneShaperAudioProcessor::VocalToneShaperAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "VocalToneShaperState", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalToneShaperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (floatParam (inputTrimId, "Input Trim", -24.0f, 24.0f, 0.0f));
    params.push_back (floatParam (outputTrimId, "Output Trim", -24.0f, 24.0f, 0.0f));
    params.push_back (floatParam (outputCeilingId, "Output Ceiling", -6.0f, 0.0f, -0.3f));
    params.push_back (boolParam (linearPhaseEqId, "Linear-Phase EQ", false));
    params.push_back (choiceParam (eqCompOrderId, "EQ Comp Order", { "EQPreComp", "CompPreEQ" }, 0));
    params.push_back (floatParam (learnedMakeupId, "Learned Makeup", -24.0f, 24.0f, 0.0f));

    params.push_back (boolParam (stageABypassId, "Stage A Bypass", false));
    params.push_back (floatParam (stageAMixId, "Stage A Mix", 0.0f, 100.0f, 100.0f));
    params.push_back (boolParam (hpfEnabledId, "HPF Enabled", true));
    params.push_back (floatParam (hpfFreqId, "HPF Frequency", 20.0f, 500.0f, 80.0f));
    params.push_back (floatParam (lowMudFreqId, "Low Mud Frequency", 100.0f, 400.0f, 200.0f));
    params.push_back (floatParam (lowMudGainId, "Low Mud Gain", -12.0f, 6.0f, -3.0f));
    params.push_back (floatParam (lowMudQId, "Low Mud Q", 0.3f, 2.0f, 0.7f));
    params.push_back (floatParam (moodFreqId, "Mood Frequency", 500.0f, 1000.0f, 750.0f));
    params.push_back (floatParam (moodGainId, "Mood Gain", -12.0f, 6.0f, -2.5f));
    params.push_back (floatParam (moodQId, "Mood Q", 0.5f, 3.0f, 1.2f));
    params.push_back (floatParam (presenceFreqId, "Presence Cut Frequency", 2000.0f, 4500.0f, 3000.0f));
    params.push_back (floatParam (presenceGainId, "Presence Cut Gain", -12.0f, 6.0f, -2.0f));
    params.push_back (floatParam (presenceQId, "Presence Cut Q", 0.3f, 2.0f, 0.8f));
    params.push_back (floatParam (airFreqId, "Air Shelf Frequency", 8000.0f, 20000.0f, 16000.0f));
    params.push_back (floatParam (airGainId, "Air Shelf Gain", -6.0f, 9.0f, 2.5f));
    params.push_back (floatParam (airQId, "Air Shelf Q", 0.4f, 1.0f, 0.6f));
    params.push_back (floatParam (driveId, "Console Drive", 0.0f, 1.0f, 0.15f, 0.001f));

    params.push_back (boolParam (stageBBypassId, "Stage B Bypass", false));
    params.push_back (floatParam (stageBMixId, "Stage B Mix", 0.0f, 100.0f, 100.0f));
    params.push_back (floatParam (stageBInputId, "FET Input Gain", -20.0f, 20.0f, 8.0f));
    params.push_back (floatParam (stageBOutputId, "FET Output Gain", -20.0f, 20.0f, -6.0f));
    params.push_back (floatParam (stageBAttackId, "FET Attack", 0.02f, 0.8f, 0.05f, 0.001f));
    params.push_back (floatParam (stageBReleaseId, "FET Release", 50.0f, 1100.0f, 60.0f));
    params.push_back (choiceParam (stageBRatioId, "FET Ratio", { "4:1", "8:1", "12:1", "20:1", "All-Buttons-In" }, 0));

    params.push_back (boolParam (stageCBypassId, "Stage C Bypass", false));
    params.push_back (floatParam (stageCMixId, "Stage C Mix", 0.0f, 100.0f, 100.0f));
    params.push_back (choiceParam (stageCPhaseId, "Stage C Phase Mode", { "Minimum", "Linear" }, 0));
    params.push_back (boolParam (stageCSubRolloffId, "Sub Rolloff", true));
    params.push_back (floatParam (stageCLowThresholdId, "Low Band Threshold", -60.0f, 0.0f, -24.0f));
    params.push_back (floatParam (stageCBoxThresholdId, "Low-Mid Threshold", -60.0f, 0.0f, -22.0f));
    params.push_back (floatParam (stageCMidThresholdId, "Mid Threshold", -60.0f, 0.0f, -20.0f));
    params.push_back (floatParam (stageCHighMidThresholdId, "High-Mid Threshold", -60.0f, 0.0f, -18.0f));
    params.push_back (floatParam (stageCHighThresholdId, "High Threshold", -60.0f, 0.0f, -16.0f));

    params.push_back (boolParam (stageDBypassId, "Stage D Bypass", false));
    params.push_back (floatParam (stageDMixId, "Stage D Mix", 0.0f, 100.0f, 100.0f));
    params.push_back (floatParam (peakReductionId, "Peak Reduction", 0.0f, 100.0f, 42.0f));
    params.push_back (floatParam (gainMakeupId, "Leveler Makeup", -20.0f, 40.0f, 0.0f));
    params.push_back (choiceParam (stageDModeId, "Leveler Mode", { "Compress", "Limit" }, 0));

    params.push_back (boolParam (stageEBypassId, "Stage E Bypass", false));
    params.push_back (floatParam (stageEMixId, "Stage E Mix", 0.0f, 100.0f, 100.0f));
    params.push_back (floatParam (midAirId, "Mid Air", 0.0f, 100.0f, 0.0f));
    params.push_back (floatParam (highAirId, "High Air", 0.0f, 100.0f, 0.0f));
    params.push_back (boolParam (trimCompensateId, "Air Trim Compensate", true));

    params.push_back (boolParam (stageFBypassId, "Stage F Bypass", false));
    params.push_back (floatParam (stageFThresholdId, "De-esser Threshold", -60.0f, 0.0f, -30.0f));
    params.push_back (floatParam (stageFFrequencyId, "De-esser Frequency", 3000.0f, 15000.0f, 7500.0f));
    params.push_back (floatParam (stageFRangeId, "De-esser Range", 0.0f, 24.0f, 12.0f));
    params.push_back (choiceParam (stageFModeId, "De-esser Mode", { "Wide", "Split" }, 1));
    params.push_back (boolParam (stageFListenId, "Listen Sidechain", false));

    return { params.begin(), params.end() };
}

float VocalToneShaperAudioProcessor::Envelope::process (float input, float attackMs, float releaseMs, double sampleRate)
{
    const auto attack = std::exp (-1.0f / static_cast<float> (0.001 * attackMs * sampleRate));
    const auto release = std::exp (-1.0f / static_cast<float> (0.001 * releaseMs * sampleRate));
    const auto coeff = input > value ? attack : release;
    value = input + coeff * (value - input);
    return value;
}

void VocalToneShaperAudioProcessor::StereoFilter::prepare (double sampleRate, int maxBlockSize)
{
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maxBlockSize), 1 };
    for (auto& filter : filters)
        filter.prepare (spec);
}

void VocalToneShaperAudioProcessor::StereoFilter::reset()
{
    for (auto& filter : filters)
        filter.reset();
}

void VocalToneShaperAudioProcessor::StereoFilter::setCoefficients (juce::dsp::IIR::Coefficients<float>::Ptr coefficients)
{
    for (auto& filter : filters)
        *filter.coefficients = *coefficients;
}

float VocalToneShaperAudioProcessor::StereoFilter::processSample (int channel, float value)
{
    return filters[static_cast<size_t> (juce::jlimit (0, 1, channel))].processSample (value);
}

void VocalToneShaperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxSamplesPerBlock = samplesPerBlock;

    for (auto* filter : { &hpf, &cSubHpf, &lowMud, &mood, &presence, &airShelf, &cLowShelf, &cBox, &cPresence,
                         &cSibilance, &eMidBand, &eHighPass, &fSidechain, &fLowPass, &fHighPass })
    {
        filter->prepare (sampleRate, samplesPerBlock);
        filter->reset();
    }

    for (auto& state : channelStates)
    {
        state.fet.reset();
        state.optoFast.reset();
        state.optoSlow.reset();
        state.deess.reset();
        state.optoReleaseMs = 800.0f;
    }

    updateFilters();
    updateLatency();
}

void VocalToneShaperAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool VocalToneShaperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}
#endif

float VocalToneShaperAudioProcessor::readFloat (const char* id) const { return readParam (parameters, id); }
int VocalToneShaperAudioProcessor::readChoice (const char* id) const { return static_cast<int> (readParam (parameters, id)); }
bool VocalToneShaperAudioProcessor::readBool (const char* id) const { return readParam (parameters, id) > 0.5f; }

void VocalToneShaperAudioProcessor::updateFilters()
{
    hpf.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, readFloat (hpfFreqId), 0.707f));
    cSubHpf.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, 91.0f, 0.707f));
    lowMud.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, readFloat (lowMudFreqId), readFloat (lowMudQId), gainFromDb (readFloat (lowMudGainId))));
    mood.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, readFloat (moodFreqId), readFloat (moodQId), gainFromDb (readFloat (moodGainId))));
    presence.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, readFloat (presenceFreqId), readFloat (presenceQId), gainFromDb (readFloat (presenceGainId))));
    airShelf.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, readFloat (airFreqId), readFloat (airQId), gainFromDb (readFloat (airGainId))));
    cLowShelf.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, 200.0f, 0.707f, gainFromDb (-1.0f)));
    cBox.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 400.0f, 0.8f, gainFromDb (-1.0f)));
    cPresence.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 1800.0f, 1.0f, gainFromDb (-0.5f)));
    cSibilance.setCoefficients (juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, 5500.0f, 1.2f, gainFromDb (-0.5f)));
    eMidBand.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, 5200.0f, 0.75f));
    eHighPass.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, 8000.0f, 0.707f));
    fSidechain.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, readFloat (stageFFrequencyId), 1.0f));
    fLowPass.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, readFloat (stageFFrequencyId), 0.707f));
    fHighPass.setCoefficients (juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, readFloat (stageFFrequencyId), 0.707f));
}

void VocalToneShaperAudioProcessor::updateLatency()
{
    auto latency = 0;
    if (readBool (linearPhaseEqId))
        latency += 128;
    if (readChoice (stageCPhaseId) == 1)
        latency += static_cast<int> (std::round (currentSampleRate * 0.010));

    setLatencySamples (latency);
}

float VocalToneShaperAudioProcessor::softClip (float x, float amount)
{
    const auto drive = 1.0f + juce::jlimit (0.0f, 1.0f, amount) * 4.0f;
    return std::tanh (x * drive) / std::tanh (drive);
}

float VocalToneShaperAudioProcessor::compressSample (float input, Envelope& detector, float thresholdDb,
                                                     float ratio, float attackMs, float releaseMs,
                                                     float rangeDb, double sampleRate, float* reductionDb)
{
    const auto envelope = detector.process (std::abs (input), attackMs, releaseMs, sampleRate);
    const auto levelDb = dbFromGain (envelope);
    const auto over = levelDb - thresholdDb;
    const auto gr = juce::jlimit (0.0f, rangeDb, over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f);

    if (reductionDb != nullptr)
        *reductionDb = gr;

    return input * gainFromDb (-gr);
}

void VocalToneShaperAudioProcessor::processStageA (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageABypassId))
        return;

    const auto mix = readFloat (stageAMixId) * 0.01f;
    const auto drive = readFloat (driveId);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            auto wet = softClip (dry, drive);
            if (readBool (hpfEnabledId))
                wet = hpf.processSample (juce::jmin (ch, 1), wet);
            wet = lowMud.processSample (juce::jmin (ch, 1), wet);
            wet = mood.processSample (juce::jmin (ch, 1), wet);
            wet = presence.processSample (juce::jmin (ch, 1), wet);
            wet = airShelf.processSample (juce::jmin (ch, 1), wet);
            data[i] = dry + (wet - dry) * mix;
        }
    }
}

void VocalToneShaperAudioProcessor::processStageB (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageBBypassId))
        return;

    const auto mix = readFloat (stageBMixId) * 0.01f;
    const auto inputGain = gainFromDb (readFloat (stageBInputId));
    const auto outputGain = gainFromDb (readFloat (stageBOutputId));
    const auto ratios = std::array<float, 5> { 4.0f, 8.0f, 12.0f, 20.0f, 30.0f };
    const auto ratio = ratios[static_cast<size_t> (juce::jlimit (0, 4, readChoice (stageBRatioId)))];
    auto maxReduction = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& state = channelStates[static_cast<size_t> (juce::jmin (ch, 1))].fet;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            auto driven = dry * inputGain;
            auto reduction = 0.0f;
            auto wet = compressSample (driven, state, -18.0f, ratio, readFloat (stageBAttackId),
                                       readFloat (stageBReleaseId), 20.0f, currentSampleRate, &reduction);
            wet = softClip (wet, 0.18f) * outputGain;
            data[i] = dry + (wet - dry) * mix;
            maxReduction = juce::jmax (maxReduction, reduction);
        }
    }

    stageBReductionDb.store (-maxReduction);
}

void VocalToneShaperAudioProcessor::processStageC (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageCBypassId))
        return;

    const auto mix = readFloat (stageCMixId) * 0.01f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto channel = juce::jmin (ch, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            auto wet = dry;
            if (readBool (stageCSubRolloffId))
                wet = cSubHpf.processSample (channel, wet);

            const auto lowAmount = juce::jmap (readFloat (stageCLowThresholdId), -60.0f, 0.0f, -4.0f, 0.0f);
            const auto boxAmount = juce::jmap (readFloat (stageCBoxThresholdId), -60.0f, 0.0f, -5.0f, 0.0f);
            const auto midAmount = juce::jmap (readFloat (stageCMidThresholdId), -60.0f, 0.0f, -3.0f, 0.0f);
            const auto highAmount = juce::jmap (readFloat (stageCHighMidThresholdId), -60.0f, 0.0f, -4.0f, 0.0f)
                                  + juce::jmap (readFloat (stageCHighThresholdId), -60.0f, 0.0f, -4.0f, 0.0f);
            wet += cLowShelf.processSample (channel, wet) * (lowAmount * 0.01f);
            wet += cBox.processSample (channel, wet) * (boxAmount * 0.01f);
            wet += cPresence.processSample (channel, wet) * (midAmount * 0.01f);
            wet += cSibilance.processSample (channel, wet) * (highAmount * 0.01f);
            data[i] = dry + (wet - dry) * mix;
        }
    }
}

void VocalToneShaperAudioProcessor::processStageD (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageDBypassId))
        return;

    const auto mix = readFloat (stageDMixId) * 0.01f;
    const auto threshold = juce::jmap (readFloat (peakReductionId), 0.0f, 100.0f, 2.0f, -34.0f);
    const auto ratio = readChoice (stageDModeId) == 1 ? 8.0f : 3.0f;
    auto maxReduction = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& state = channelStates[static_cast<size_t> (juce::jmin (ch, 1))];
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            auto reduction = 0.0f;
            const auto fast = compressSample (dry, state.optoFast, threshold, ratio, 10.0f, 60.0f, 15.0f, currentSampleRate, nullptr);
            auto wet = compressSample (dry, state.optoSlow, threshold, ratio, 10.0f, state.optoReleaseMs, 15.0f, currentSampleRate, &reduction);
            wet = fast * 0.4f + wet * 0.6f;
            state.optoReleaseMs = juce::jlimit (500.0f, 5000.0f, state.optoReleaseMs + (reduction > 3.0f ? 2.0f : -1.0f));
            wet = softClip (wet, 0.035f) * gainFromDb (readFloat (gainMakeupId));
            data[i] = dry + (wet - dry) * mix;
            maxReduction = juce::jmax (maxReduction, reduction);
        }
    }

    stageDReductionDb.store (-maxReduction);
}

void VocalToneShaperAudioProcessor::processStageE (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageEBypassId))
        return;

    const auto mix = readFloat (stageEMixId) * 0.01f;
    const auto midAmount = readFloat (midAirId) * 0.01f;
    const auto highAmount = readFloat (highAirId) * 0.01f;
    const auto dryTrim = readBool (trimCompensateId) ? 1.0f - 0.15f * (midAmount + highAmount) * 0.5f : 1.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        const auto channel = juce::jmin (ch, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            const auto mid = eMidBand.processSample (channel, dry);
            const auto high = eHighPass.processSample (channel, dry);
            const auto excited = (mid + 2.0f * mid * mid - 0.5f * mid * mid * mid) * midAmount * 0.35f
                               + (high + 2.0f * high * high - 0.5f * high * high * high) * highAmount * 0.35f;
            const auto wet = dry * dryTrim + excited;
            data[i] = dry + (wet - dry) * mix;
        }
    }
}

void VocalToneShaperAudioProcessor::processStageF (juce::AudioBuffer<float>& buffer)
{
    if (readBool (stageFBypassId))
        return;

    auto maxReduction = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& state = channelStates[static_cast<size_t> (juce::jmin (ch, 1))].deess;
        const auto channel = juce::jmin (ch, 1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            const auto sidechain = fSidechain.processSample (channel, dry);
            auto reduction = 0.0f;
            const auto ignored = compressSample (sidechain, state, readFloat (stageFThresholdId), 10.0f, 0.1f,
                                                 30.0f, readFloat (stageFRangeId), currentSampleRate, &reduction);
            juce::ignoreUnused (ignored);

            if (readBool (stageFListenId))
            {
                data[i] = sidechain;
                continue;
            }

            const auto attenuation = gainFromDb (-reduction);
            if (readChoice (stageFModeId) == 0)
                data[i] = dry * attenuation;
            else
                data[i] = fLowPass.processSample (channel, dry) + fHighPass.processSample (channel, dry) * attenuation;

            maxReduction = juce::jmax (maxReduction, reduction);
        }
    }

    stageFReductionDb.store (-maxReduction);
}

void VocalToneShaperAudioProcessor::processLimiter (juce::AudioBuffer<float>& buffer)
{
    const auto ceiling = gainFromDb (readFloat (outputCeilingId));
    auto peak = 0.0f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            auto x = data[i];
            if (std::abs (x) > ceiling)
                x = std::copysign (ceiling + std::tanh ((std::abs (x) - ceiling) / ceiling) * ceiling * 0.05f, x);

            data[i] = juce::jlimit (-1.0f, 1.0f, x);
            peak = juce::jmax (peak, std::abs (data[i]));
        }
    }

    outputPeakDb.store (dbFromGain (peak));
}

void VocalToneShaperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalIn = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();
    const auto processChannels = juce::jmin (buffer.getNumChannels(), totalOut);

    auto inputPeak = 0.0f;
    for (int channel = 0; channel < juce::jmin (buffer.getNumChannels(), totalIn); ++channel)
        inputPeak = juce::jmax (inputPeak, buffer.getMagnitude (channel, 0, buffer.getNumSamples()));
    inputPeakDb.store (dbFromGain (inputPeak));

    for (int channel = totalIn; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (processChannels >= 2 && totalIn == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    if (--filterUpdateCountdown <= 0)
    {
        filterUpdateCountdown = 128;
        updateFilters();
        updateLatency();
    }

    buffer.applyGain (gainFromDb (readFloat (inputTrimId)));

    if (readChoice (eqCompOrderId) == 0)
    {
        processStageA (buffer);
        processStageB (buffer);
    }
    else
    {
        processStageB (buffer);
        processStageA (buffer);
    }

    processStageC (buffer);
    processStageD (buffer);
    processStageE (buffer);
    processStageF (buffer);

    buffer.applyGain (gainFromDb (readFloat (outputTrimId) + readFloat (learnedMakeupId)));
    processLimiter (buffer);
    sanitizeBuffer (buffer);
}

void VocalToneShaperAudioProcessor::sanitizeBuffer (juce::AudioBuffer<float>& buffer)
{
    const auto channels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    for (int ch = 0; ch < channels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (! std::isfinite (data[i]))
                data[i] = 0.0f;
        }
    }
}

juce::AudioProcessorEditor* VocalToneShaperAudioProcessor::createEditor()
{
    return new VocalToneShaperAudioProcessorEditor (*this);
}

void VocalToneShaperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalToneShaperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalToneShaperAudioProcessor();
}
