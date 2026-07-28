#include "AnalysisCore.h"

//==============================================================================
void AnalysisCore::prepare (double sampleRate, int /*maxBlockSize*/)
{
    fft = std::make_unique<juce::dsp::FFT> (kFFTOrder);
    fftBuffer.setSize (1, kFFTSize, false, true);
    fftBuffer.clear();
    fftWritePos = 0;

    // Control rate: ~172 Hz at 44.1k (256-sample update interval)
    fftCountdown = 0;

    computeBands (sampleRate);
}

void AnalysisCore::reset()
{
    if (fftBuffer.getNumSamples() > 0)
        fftBuffer.clear();
    fftWritePos = 0;
    fftCountdown = 0;
    cv = ControlVector{};
}

//==============================================================================
void AnalysisCore::computeBands (double sampleRate)
{
    const float nyquist = static_cast<float> (sampleRate * 0.5);
    const float binWidth = nyquist / static_cast<float> (kFFTSize / 2);

    // 8 log-spaced bands from 20 Hz to nyquist
    const float logLow  = std::log10 (20.0f);
    const float logHigh = std::log10 (nyquist);

    for (int i = 0; i <= kNumBands; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (kNumBands);
        const float freq = std::pow (10.0f, logLow + t * (logHigh - logLow));
        bandBins[i] = juce::jlimit (1, kFFTSize / 2,
                                     juce::roundToInt (freq / binWidth));
    }
}

//==============================================================================
void AnalysisCore::process (const juce::AudioBuffer<float>& buffer,
                            int numSamples, int numChannels)
{
    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Accumulate mono-summed samples into FFT buffer
    auto* fftData = fftBuffer.getWritePointer (0);
    const int toCopy = juce::jmin (numSamples, kFFTSize - fftWritePos);

    for (int i = 0; i < toCopy; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getSample (ch, i);
        fftData[fftWritePos + i] = sum / static_cast<float> (numChannels);
    }
    fftWritePos += toCopy;

    // If the buffer isn't full yet, keep accumulating
    if (fftWritePos < kFFTSize)
        return;

    // Perform FFT
    fft->performFrequencyOnlyForwardTransform (fftData);

    // Update control vector from the spectrum
    updateControlVector (fftData, numChannels, buffer, numSamples);

    // Reset write position for the next window
    fftWritePos = 0;
}

//==============================================================================
void AnalysisCore::updateControlVector (const float* spectrum,
                                        int numChannels,
                                        const juce::AudioBuffer<float>& buffer,
                                        int numSamples)
{
    // Accumulate band energies from the magnitude spectrum
    float bandEnergy[kNumBands] = {};
    float totalEnergy = 0.0f;

    for (int b = 0; b < kNumBands; ++b)
    {
        const int startBin = bandBins[b];
        const int endBin   = bandBins[b + 1];
        float sum = 0.0f;
        for (int i = startBin; i < endBin; ++i)
            sum += spectrum[i] * spectrum[i];
        bandEnergy[b] = sum / static_cast<float> (endBin - startBin);
        totalEnergy += bandEnergy[b];
    }

    // Guard against silence
    const float epsilon = 1e-12f;
    if (totalEnergy < epsilon)
    {
        cv = ControlVector{};
        cv.activity = 0.0f;
        return;
    }

    // --- Harmonic Density: energy in mid-bands (bands 2-5, ~200 Hz – 5 kHz) ---
    float midEnergy = 0.0f;
    for (int b = 2; b <= 5; ++b)
        midEnergy += bandEnergy[b];
    cv.harmonicDensity = juce::jlimit (0.0f, 1.0f, midEnergy / totalEnergy);

    // --- Spectral Flatness: geometric mean / arithmetic mean across bands ---
    float arithMean = 0.0f;
    float geoMean   = 0.0f;
    int activeBands = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (bandEnergy[b] > epsilon)
        {
            arithMean += bandEnergy[b];
            geoMean   += std::log (bandEnergy[b]);
            ++activeBands;
        }
    }
    if (activeBands > 0)
    {
        arithMean /= static_cast<float> (activeBands);
        geoMean = std::exp (geoMean / static_cast<float> (activeBands));
        cv.spectralFlatness = juce::jlimit (0.0f, 1.0f, geoMean / (arithMean + epsilon));
    }
    else
    {
        cv.spectralFlatness = 1.0f;
    }

    // --- Crest Factor: peak / RMS across all channels ---
    float peak = 0.0f;
    float rmsAccum = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            const float absVal = std::abs (data[i]);
            if (absVal > peak)
                peak = absVal;
            rmsAccum += data[i] * data[i];
        }
    }
    const float rms = std::sqrt (rmsAccum / static_cast<float> (numSamples * numChannels));
    // Normalize crest factor: typical range ~1.0 (sine) to ~5+ (very peaky).
    // Map 1.0→0.0, 5.0→1.0, clamp.
    const float rawCrest = (rms > epsilon) ? (peak / rms) : 1.0f;
    cv.crestFactor = juce::jlimit (0.0f, 1.0f, (rawCrest - 1.0f) / 4.0f);

    // --- Brightness: high-band energy (bands 6-7, ~5 kHz+) / total ---
    float highEnergy = 0.0f;
    for (int b = 6; b < kNumBands; ++b)
        highEnergy += bandEnergy[b];
    cv.brightness = juce::jlimit (0.0f, 1.0f, highEnergy / totalEnergy);

    // --- Activity: RMS normalized (0 dBFS = 1.0) ---
    cv.activity = juce::jlimit (0.0f, 1.0f, rms * 3.0f); // scale so typical signals show activity
}
