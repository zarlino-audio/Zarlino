#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    //--------------------------------------------------------------------------
    // Periodic Hann window (denominator N, not N-1) — required for EXACT COLA
    // at 50% overlap: w[n] + w[n+N/2] = 1. The symmetric variant (denominator
    // N-1) only approximates this, which causes ~6 dB amplitude ripple when the
    // window is applied in both analysis and synthesis.
    static void fillHannWindow (std::vector<float>& window)
    {
        const auto N = static_cast<int> (window.size());
        for (int i = 0; i < N; ++i)
            window[static_cast<size_t> (i)] = 0.5f * (1.0f - std::cos (
                juce::MathConstants<float>::twoPi * i / N));
    }

    //--------------------------------------------------------------------------
    // Clamp a value to [lo, hi]
    template <typename T>
    static T clamped (T value, T lo, T hi)
    {
        return (value < lo) ? lo : (value > hi) ? hi : value;
    }

    //--------------------------------------------------------------------------
    // Exponential smoothing coefficient from time-constant in samples
    static float timeConstantToCoeff (float timeSec, float sampleRate)
    {
        if (timeSec <= 0.0f || sampleRate <= 0.0f)
            return 1.0f;

        const auto n = static_cast<float> (timeSec * sampleRate);
        if (n <= 0.0f)
            return 1.0f;

        return 1.0f - std::exp (-1.0f / n);
    }

    //--------------------------------------------------------------------------
    // Resonant peak detection: how many bins either side to build the
    // spectral neighbourhood.  This is frequency-dependent for musicality:
    // wider at high frequencies, narrower at low frequencies.
    static int neighbourhoodRadius (int binIndex, int sampleRate, int fftSize)
    {
        const auto freq = static_cast<float> (binIndex) * sampleRate / fftSize;
        // ~1 critical band width in bins: grow from 1 bin at low freqs
        // to ~8 bins at high freqs
        return 1 + static_cast<int> (std::sqrt (freq / 1000.0f) * 2.5f);
    }

    //--------------------------------------------------------------------------
    // Convert a ratio to dB
    static float ratioToDb (float ratio)
    {
        return juce::Decibels::gainToDecibels (ratio, -120.0f);
    }

} // anonymous namespace

//==============================================================================
ZTameAudioProcessor::ZTameAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ZTameState", createParameterLayout())
{
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZTameAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Sensitivity — how easily a peak triggers suppression (0-100%)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramSensitivity, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 50.0f));

    // Range — maximum gain reduction in dB (0–24 dB)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramRange, "Range",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.05f, 0.7f), 12.0f));

    // Selectivity — how sharply defined a peak must be (0-100%).
    // Maps to the ratio threshold: low selectivity = 3 dB (gentle),
    // high selectivity = 12 dB (aggressive).
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramSelectivity, "Selectivity",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 50.0f));

    // Crossover frequency for split-band mode
    // Range 20–20000 Hz, default 0 means "off" (full-band)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramCrossoverFreq, "Split Crossover",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 0.0f));

    // Attack time in ms (0.1–100 ms)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramAttack, "Attack",
        juce::NormalisableRange<float> (0.1f, 100.0f, 0.05f, 0.4f), 3.0f));

    // Release time in ms (10–1000 ms)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramRelease, "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.5f), 100.0f));

    // Listen/Solo toggle — hear only the suppressed material
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramListenMode, "Listen Mode", false));

    // Dry/Wet mix (0-100%)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        paramDryWet, "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f, 0.5f), 100.0f));

    // Master bypass
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        paramBypass, "Bypass", false));

    return { params.begin(), params.end() };
}

//==============================================================================
float ZTameAudioProcessor::readFloatParam (
    juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (auto* param = state.getRawParameterValue (id))
        return param->load();

    return 0.0f;
}

//==============================================================================
void ZTameAudioProcessor::initChannelState (ChannelState& state,
                                             double sampleRate)
{
    (void) sampleRate;

    // Input ring buffer: hold 2 FFT frames
    const auto bufSize = static_cast<size_t> (fftSize * 2);
    state.inputBuffer.assign (bufSize, 0.0f);
    state.inputWritePos = 0;

    // Output overlap-add accumulator: hold 2 FFT frames
    state.outputAccum.assign (bufSize, 0.0f);
    state.outputWritePos = 0;

    // FFT work buffer — JUCE's real FFT requires 2 * fftSize (first half = real
    // input, second half = packed spectrum + scratch). A fftSize-sized buffer
    // overflows and corrupts the heap.
    state.fftBuffer.assign (static_cast<size_t> (fftSize * 2), 0.0f);

    // Hann window
    state.window.resize (static_cast<size_t> (fftSize));
    fillHannWindow (state.window);

    // Per-bin gain states
    state.binGain.assign (static_cast<size_t> (numBins), 1.0f);
    state.binGainSmooth.assign (static_cast<size_t> (numBins), 1.0f);

    // Magnitude buffer
    state.magnitude.assign (static_cast<size_t> (numBins), 0.0f);

    state.samplesSinceFFT = fftSize; // force first frame immediately
    state.prepared = true;
}

//==============================================================================
void ZTameAudioProcessor::prepareToPlay (double sampleRate,
                                          int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxSamplesPerBlock = samplesPerBlock;

    for (auto& ch : channels)
    {
        initChannelState (ch, sampleRate);
    }
}

void ZTameAudioProcessor::releaseResources()
{
    for (auto& ch : channels)
    {
        ch = ChannelState{};
    }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ZTameAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

//==============================================================================
void ZTameAudioProcessor::computeMagnitude (
    const std::vector<float>& spectrum,
    std::vector<float>& magnitude) const
{
    // The real FFT packs bin k at floats [2k, 2k+1] (re, im) for k in 0..N/2:
    //   bin 0 (DC)          -> spectrum[0]
    //   bin k (1..N/2-1)    -> spectrum[2k], spectrum[2k+1]
    //   bin N/2 (Nyquist)   -> spectrum[N], spectrum[N+1]
    // Magnitude = sqrt(re^2 + im^2) for each bin.
    for (int i = 0; i < numBins; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        if (i == 0)
        {
            // DC bin — just real
            magnitude[idx] = std::abs (spectrum[0]);
        }
        else if (i < fftHalf)
        {
            const auto re = spectrum[idx * 2u];
            const auto im = spectrum[idx * 2u + 1u];
            magnitude[idx] = std::sqrt (re * re + im * im);
        }
        else
        {
            // Nyquist bin
            const auto re = spectrum[static_cast<size_t> (fftSize)];
            const auto im = spectrum[static_cast<size_t> (fftSize) + 1u];
            magnitude[idx] = std::sqrt (re * re + im * im);
        }
    }
}

//==============================================================================
void ZTameAudioProcessor::computeBinGains (
    const std::vector<float>& magnitude,
    std::vector<float>& gains,
    double sampleRate,
    float sensitivity,
    float rangeFactor,
    float selectivityFactor,
    float crossoverHz,
    bool splitBandActive) const
{
    // sensitivity (0-1): offset above local average to trigger
    // selectivity (0-1): scales the ratio threshold
    //
    // threshold_dB = sensitivity * (1.0 + selectivity * 2.0)
    // i.e. with max sensitivity & max selectivity, threshold ≈ 3 dB
    //      with min sensitivity & min selectivity, threshold ≈ 24 dB

    const auto sensNorm   = clamped (sensitivity / 100.0f, 0.0f, 1.0f);
    const auto rangeDb    = rangeFactor; // already in dB
    const auto selNorm    = clamped (selectivityFactor / 100.0f, 0.0f, 1.0f);

    // The threshold in dB above local average to trigger suppression.
    // Maps from 24 dB (gentle, low sens+sel) down to 1.5 dB (aggressive).
    const auto thresholdDb = 24.0f - sensNorm * (12.0f + selNorm * 10.5f);
    const auto thresholdLin = juce::Decibels::decibelsToGain (thresholdDb);

    // Maximum reduction factor (linear)
    const auto maxReductionLin = juce::Decibels::decibelsToGain (-rangeDb);

    // Compute per-bin gain
    for (int i = 0; i < numBins; ++i)
    {
        const auto idx = static_cast<size_t> (i);

        // Skip DC and Nyquist bins — they rarely carry resonant content
        if (i == 0 || i == fftHalf)
        {
            gains[idx] = 1.0f;
            continue;
        }

        // ---- local neighbourhood average ----
        const auto radius = neighbourhoodRadius (i, static_cast<int> (sampleRate), fftSize);
        const auto startBin = std::max (1, i - radius);
        const auto endBin   = std::min (fftHalf - 1, i + radius);

        float sum = 0.0f;
        int count = 0;
        for (int j = startBin; j <= endBin; ++j)
        {
            if (j == i) continue; // exclude self
            sum += magnitude[static_cast<size_t> (j)];
            ++count;
        }
        const auto localAvg = (count > 0) ? (sum / count) : magnitude[idx];

        // ---- resonance ratio ----
        const auto binMag = magnitude[idx];
        const auto resonance = (localAvg > 1e-10f) ? (binMag / localAvg) : 1.0f;

        // If the bin is quiet (< -100 dBFS), don't touch it
        if (binMag < 1e-6f)
        {
            gains[idx] = 1.0f;
            continue;
        }

        // ---- split-band gating ----
        // When split-band is active, each bin is only suppressed by peaks
        // within its own band (low/high). This prevents high-frequency
        // harshness from suppressing low-end content, and vice versa.
        if (splitBandActive && crossoverHz > 0.0f)
        {
            const auto binFreq = static_cast<float> (i) * static_cast<float> (sampleRate) / static_cast<float> (fftSize);
            // If the current bin is in the opposite band from the detected peak
            // (resonance > threshold), don't suppress this bin.
            // Simplified: check if this bin's frequency is on the opposite side
            // of the crossover from where the suppression would be most active.
            // For a real split-band implementation, we'd compute separate gain
            // curves for low and high bands. For this initial version, we just
            // note the bin frequency for future use.
            (void) binFreq;
        }

        // ---- gain computation ----
        if (resonance <= thresholdLin)
        {
            gains[idx] = 1.0f;
        }
        else
        {
            // Map resonance[threshold..inf) to gain[1..maxReduction]
            // The stronger the resonance, the deeper the cut
            const auto excess = (resonance - thresholdLin) / (resonance + 1.0f);
            const auto gainReduction = 1.0f - excess * (1.0f - maxReductionLin);
            gains[idx] = std::max (maxReductionLin, std::min (1.0f, gainReduction));
        }
    }
}

//==============================================================================
void ZTameAudioProcessor::applyGainsAndSynthesise (
    const std::vector<float>& gains,
    std::vector<float>& spectrum) const
{
    // Apply per-bin gain to the complex FFT data (JUCE half-complex packing:
    // bin k re@2k, im@2k+1; Nyquist re@N, im@N+1).
    for (int i = 0; i < numBins; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const auto g = gains[idx];

        if (i == 0)
        {
            spectrum[0] *= g;
        }
        else if (i < fftHalf)
        {
            spectrum[idx * 2u]      *= g;
            spectrum[idx * 2u + 1u] *= g;
        }
        else
        {
            spectrum[static_cast<size_t> (fftSize)]      *= g;
            spectrum[static_cast<size_t> (fftSize) + 1u] *= g;
        }
    }

    // IFFT performed by caller (they own the buffer)
}

//==============================================================================
void ZTameAudioProcessor::processFrame (ChannelState& state, int /*channelIndex*/)
{
    if (! fft)
        return;

    if (! state.prepared
        || state.inputBuffer.size() < static_cast<size_t> (fftSize * 2)
        || state.outputAccum.size() < static_cast<size_t> (fftSize * 2)
        || state.fftBuffer.size() < static_cast<size_t> (fftSize * 2)
        || state.window.size() < static_cast<size_t> (fftSize))
    {
        return;
    }

    // ---- extract frame from input ring buffer ----
    const auto readPos = (state.inputWritePos - fftSize + fftSize * 2) % (fftSize * 2);

    for (int i = 0; i < fftSize; ++i)
    {
        const auto bufIdx = static_cast<size_t> ((readPos + i) % (fftSize * 2));
        state.fftBuffer[static_cast<size_t> (i)] =
            state.inputBuffer[bufIdx] * state.window[static_cast<size_t> (i)];
    }

    // ---- forward FFT (only non-negative half needed; buffer is 2*fftSize) ----
    fft->performRealOnlyForwardTransform (state.fftBuffer.data(), true);

    // ---- magnitude spectrum ----
    computeMagnitude (state.fftBuffer, state.magnitude);

    // ---- read parameters ----
    const auto sensitivity   = readFloatParam (parameters, paramSensitivity);
    const auto range         = readFloatParam (parameters, paramRange);
    const auto selectivity   = readFloatParam (parameters, paramSelectivity);
    const auto crossoverHz   = readFloatParam (parameters, paramCrossoverFreq);
    const auto attackMs      = readFloatParam (parameters, paramAttack);
    const auto releaseMs     = readFloatParam (parameters, paramRelease);
    const auto listenMode    = readFloatParam (parameters, paramListenMode) > 0.5f;
    const auto bypass        = readFloatParam (parameters, paramBypass) > 0.5f;

    const bool splitBandActive = crossoverHz > 1.0f;

    // ---- compute target gains ----
    computeBinGains (state.magnitude, state.binGain,
                     currentSampleRate, sensitivity, range, selectivity,
                     crossoverHz, splitBandActive);

    // ---- smooth gains with attack/release ----
    const auto attackCoeff  = timeConstantToCoeff (attackMs  * 0.001f,
                                                    static_cast<float> (currentSampleRate));
    const auto releaseCoeff = timeConstantToCoeff (releaseMs * 0.001f,
                                                    static_cast<float> (currentSampleRate));

    float totalGainReduction = 0.0f;
    int gainReductionCount = 0;

    for (int i = 0; i < numBins; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const auto target = state.binGain[idx];
        auto& smooth = state.binGainSmooth[idx];

        if (bypass)
        {
            smooth = 1.0f;
        }
        else
        {
            if (target < smooth)
                smooth += (target - smooth) * attackCoeff;
            else
                smooth += (target - smooth) * releaseCoeff;
        }

        // Track average gain reduction for metering
        if (smooth < 0.999f)
        {
            totalGainReduction += ratioToDb (smooth);
            ++gainReductionCount;
        }
    }

    // Update metering (smoothed over frames)
    if (gainReductionCount > 0)
    {
        const auto avgReduction = totalGainReduction / gainReductionCount;
        const auto currentGR = gainReductionDb.load();
        gainReductionDb.store (currentGR + (avgReduction - currentGR) * 0.1f);
    }
    else
    {
        const auto currentGR = gainReductionDb.load();
        gainReductionDb.store (currentGR * 0.95f); // decay when no reduction
    }

    // ---- apply gains (or listen mode) ----
    if (listenMode && ! bypass)
    {
        // Listen/Solo mode: output the *difference* (the suppressed material)
        // Difference = dry gain - wet gain per bin
        // We need the original spectrum first, then the processed one.
        // Since we only have one FFT buffer, we'll compute the suppressed
        // spectrum as: (1 - smooth_gain) * original_spectrum
        // which is equivalent to original - processed.
        for (int i = 0; i < numBins; ++i)
        {
            const auto idx = static_cast<size_t> (i);
            const auto g = state.binGainSmooth[idx];
            const auto suppression = 1.0f - g;  // amount of material being removed

            if (i == 0)
            {
                state.fftBuffer[0] *= suppression;
            }
            else if (i < fftHalf)
            {
                state.fftBuffer[idx * 2u]      *= suppression;
                state.fftBuffer[idx * 2u + 1u] *= suppression;
            }
            else
            {
                state.fftBuffer[static_cast<size_t> (fftSize)]      *= suppression;
                state.fftBuffer[static_cast<size_t> (fftSize) + 1u] *= suppression;
            }
        }
    }
    else if (! bypass)
    {
        applyGainsAndSynthesise (state.binGainSmooth, state.fftBuffer);
    }

    // ---- inverse FFT ----
    fft->performRealOnlyInverseTransform (state.fftBuffer.data());

    // ---- overlap-add to output accumulator ----
    // Window is applied in ANALYSIS only (synthesis just overlaps the inverse
    // frames). The FFT engine is NORMALISED (forward+inverse = identity), and
    // the periodic Hann satisfies w[n] + w[n+N/2] = 1 exactly at 50% overlap,
    // so the two overlapping frames sum to unity — no gain needed.
    const auto outSize = static_cast<size_t> (fftSize * 2);
    const auto wolaGain = 1.0f;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto idx = static_cast<size_t> (i);
        const auto accPos = (state.outputWritePos + i) % outSize;
        state.outputAccum[accPos] += state.fftBuffer[idx] * wolaGain;
    }

    state.outputWritePos = (state.outputWritePos + hopSize) % (fftSize * 2);
}

//==============================================================================
void ZTameAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const auto numChannels = std::min (getTotalNumOutputChannels(), 2);
    if (numChannels <= 0)
        return;

    // Read dry/wet for final mix
    const auto dryWet = readFloatParam (parameters, paramDryWet) / 100.0f;

    // True bypass: skip the entire FFT chain when bypass is active AND dry/wet is 100%
    const auto bypassActive = readFloatParam (parameters, paramBypass) > 0.5f;
    if (bypassActive && dryWet >= 1.0f)
    {
        // Clear extra output channels but leave processed channels untouched (passthrough)
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);
        return;
    }

    // Save dry buffer if wet < 100%
    juce::AudioBuffer<float> dryBuffer;
    if (dryWet < 1.0f)
    {
        dryBuffer.makeCopyOf (buffer);
    }

    // Clear any extra output channels
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Process each channel
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& state = channels[static_cast<size_t> (ch)];
        if (! state.prepared
            || state.inputBuffer.size() < static_cast<size_t> (fftSize * 2)
            || state.outputAccum.size() < static_cast<size_t> (fftSize * 2)
            || state.fftBuffer.size() < static_cast<size_t> (fftSize * 2)
            || state.window.size() < static_cast<size_t> (fftSize))
        {
            continue;
        }

        const auto* inputData = buffer.getReadPointer (ch);
        auto* outputData = buffer.getWritePointer (ch);

        for (int s = 0; s < numSamples; ++s)
        {
            const auto idx = static_cast<size_t> (s);

            // 1. Write input sample to ring buffer
            state.inputBuffer[static_cast<size_t> (state.inputWritePos)] = inputData[idx];
            state.inputWritePos = (state.inputWritePos + 1) % (fftSize * 2);

            // 2. Read output sample from overlap-add accumulator.
            // The read cursor runs one hop BEHIND the write cursor: during the
            // cycle where the write pointer is W, positions [W-H, W-1] are
            // covered by exactly the two frames written at W-2H and W-H — both
            // fully written, not yet overwritten. Reading W+samplesSinceFFT
            // (the old code) sampled positions the NEXT frame is about to
            // overwrite, which zeroed the overlap-add and killed the signal.
            const auto outReadPos = static_cast<size_t> (
                (state.outputWritePos - hopSize + state.samplesSinceFFT + fftSize * 2) % (fftSize * 2));
            outputData[idx] = state.outputAccum[outReadPos];
            state.outputAccum[outReadPos] = 0.0f; // clear consumed sample

            // 3. Check if it's time to process a new FFT frame
            ++state.samplesSinceFFT;
            if (state.samplesSinceFFT >= hopSize)
            {
                state.samplesSinceFFT = 0;
                processFrame (state, ch);
            }
        }
    }

    // ---- dry/wet mix ----
    if (dryWet < 1.0f)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* outData = buffer.getWritePointer (ch);
            const auto* dryData = dryBuffer.getReadPointer (ch);
            for (int s = 0; s < numSamples; ++s)
            {
                outData[s] = dryData[s] * (1.0f - dryWet)
                           + outData[s] * dryWet;
            }
        }
    }
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
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ZTameAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr)
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// This creates new instances of the plugin for the host to use
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZTameAudioProcessor();
}
