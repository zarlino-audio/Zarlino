#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <cstdint>

// ZAliasingTest: Measures aliasing products in DSP processor output.
// Generates a high-frequency sine sweep, passes it through the processor,
// and measures harmonic + aliasing distortion relative to the fundamental.
//
// Usage pattern:
//   ZAliasingTest tester;
//   tester.prepare(sampleRate);
//   auto results = tester.measureAliasing([](float* buf, int n) {
//       // your DSP processor function
//   });

class ZAliasingTest
{
public:
    ZAliasingTest() = default;

    /** Configure the analyzer for a given sample rate and FFT size.
     *  Larger FFT = better frequency resolution for aliasing products.
     */
    void prepare(double sampleRate, int fftOrder = 14)
    {
        m_sampleRate = sampleRate;
        m_fftOrder = fftOrder;
        m_fftSize = 1 << m_fftOrder;

        m_fft = std::make_unique<juce::dsp::FFT>(m_fftOrder);
        m_fftBuffer.resize(2 * m_fftSize, 0.0f);
        m_window.resize(m_fftSize, 0.0f);

        // Hann window
        for (int i = 0; i < m_fftSize; ++i)
        {
            m_window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                                   * static_cast<float>(i)
                                                   / static_cast<float>(m_fftSize - 1)));
        }
    }

    struct AliasResults
    {
        double fundamentalDb = 0.0;       // Fundamental level (dB)
        double thdDb = -120.0;            // Total harmonic distortion (dB)
        double highestAliasDb = -120.0;   // Highest aliasing product (dB)
        double aliasFloorDb = -120.0;     // Average aliasing floor (dB)
        double testFrequencyHz = 0.0;
        double sampleRateHz = 0.0;
        std::vector<std::pair<double, double>> aliasPeaks; // (Hz, dB) pairs
    };

    /** Measure aliasing by processing a sine tone through a DSP function.
     *
     *  @param processFn  Callable: void(float* buffer, int numSamples)
     *                    The buffer initially contains a sine tone. The function
     *                    should process it in-place.
     *  @param testFreqHz Frequency of the test sine in Hz.
     *                    Use high frequencies (e.g., 8–18 kHz) to expose aliasing.
     *  @param levelDb    Level of the test tone in dBFS (default: -6 dB).
     *  @return Aliasing measurement results.
     */
    template <typename F>
    AliasResults measureAliasing(F&& processFn, double testFreqHz, float levelDb = -6.0f)
    {
        AliasResults results;
        results.testFrequencyHz = testFreqHz;
        results.sampleRateHz = m_sampleRate;

        const float amplitude = std::pow(10.0f, levelDb / 20.0f);
        const int numBlocks = 4; // steady-state after a few blocks

        // Zero all buffers
        std::fill(m_fftBuffer.begin(), m_fftBuffer.end(), 0.0f);

        // Process several blocks to let state settle, then capture the last one
        std::vector<float> block(m_fftSize);
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < m_fftSize; ++i)
            {
                const double phase = 2.0 * juce::MathConstants<double>::pi
                                   * testFreqHz * static_cast<double>(i + b * m_fftSize)
                                   / m_sampleRate;
                block[i] = amplitude * static_cast<float>(std::sin(phase));
            }

            processFn(block.data(), m_fftSize);
        }

        // Apply window and copy to FFT buffer (interleaved real/imag)
        for (int i = 0; i < m_fftSize; ++i)
        {
            m_fftBuffer[2 * i] = block[i] * m_window[i];
            m_fftBuffer[2 * i + 1] = 0.0f;
        }

        // Forward FFT
        m_fft->performRealOnlyForwardTransform(m_fftBuffer.data(), true);

        // Analyze spectrum
        const int numBins = m_fftSize / 2 + 1;
        std::vector<float> magnitude(numBins);

        // Bin for fundamental
        const double binWidth = m_sampleRate / static_cast<double>(m_fftSize);
        const int fundamentalBin = static_cast<int>(testFreqHz / binWidth + 0.5);

        for (int i = 0; i < numBins; ++i)
        {
            const float re = m_fftBuffer[2 * i];
            const float im = m_fftBuffer[2 * i + 1];
            magnitude[i] = std::sqrt(re * re + im * im);
        }

        // Fundamental level
        results.fundamentalDb = amplitudeToDb(magnitude[fundamentalBin]);

        // Harmonic + alias analysis: look for peaks above noise floor
        const float noiseFloor = estimateNoiseFloor(magnitude, fundamentalBin);
        float harmSum = 0.0f;
        float maxAlias = 0.0f;
        float aliasSum = 0.0f;
        int aliasCount = 0;

        // Scan for harmonic peaks (multiples of fundamental)
        for (int h = 2; h <= 10; ++h)
        {
            const int harmonicBin = fundamentalBin * h;
            if (harmonicBin >= numBins)
                continue; // harmonic above Nyquist, handled via aliasing below
            if (harmonicBin < numBins && harmonicBin > fundamentalBin + 2)
            {
                // Check if this harmonic could have aliased
                const double harmonicFreq = h * testFreqHz;
                const double nyquist = m_sampleRate / 2.0;

                float peakMag = findPeak(magnitude, harmonicBin, 3);
                if (peakMag > noiseFloor)
                {
                    harmSum += peakMag * peakMag;

                    // Aliasing: harmonic above Nyquist folded back
                    if (harmonicFreq > nyquist)
                    {
                        const double aliasedFreq = std::abs(harmonicFreq
                            - nyquist * 2.0 * std::round(harmonicFreq / (nyquist * 2.0)));
                        const int aliasBin = static_cast<int>(aliasedFreq / binWidth + 0.5);
                        if (aliasBin < numBins)
                        {
                            float aliasMag = findPeak(magnitude, aliasBin, 3);
                            if (aliasMag > maxAlias) maxAlias = aliasMag;
                            aliasSum += aliasMag * aliasMag;
                            aliasCount++;
                            results.aliasPeaks.push_back({aliasedFreq, amplitudeToDb(aliasMag)});
                        }
                    }
                }
            }
        }

        // THD
        const float fundMag = magnitude[fundamentalBin];
        if (fundMag > noiseFloor)
            results.thdDb = 10.0 * std::log10(harmSum / (fundMag * fundMag));
        else
            results.thdDb = 0.0;

        // Alias metrics
        results.highestAliasDb = (maxAlias > noiseFloor)
            ? amplitudeToDb(maxAlias) : results.aliasFloorDb;

        results.aliasFloorDb = (aliasCount > 0)
            ? 10.0 * std::log10(aliasSum / static_cast<float>(aliasCount))
            : results.aliasFloorDb;

        return results;
    }

    /** Quick check: is aliasing below a given threshold?
     *  Typical targets: ≤ -80 dB for "clean", ≤ -60 dB for "acceptable saturation".
     */
    template <typename F>
    bool aliasingBelowThreshold(F&& processFn, double testFreqHz,
                                 float thresholdDb = -80.0f, float levelDb = -6.0f)
    {
        auto results = measureAliasing(processFn, testFreqHz, levelDb);
        return results.highestAliasDb <= thresholdDb;
    }

private:
    static float amplitudeToDb(float amp)
    {
        const float minVal = 1e-12f;
        return 20.0f * std::log10(std::max(amp, minVal));
    }

    float estimateNoiseFloor(const std::vector<float>& magnitude, int excludeBin)
    {
        // Median magnitude as noise floor estimate (robust to peaks)
        std::vector<float> sorted(magnitude.size());
        int idx = 0;
        for (size_t i = 0; i < magnitude.size(); ++i)
        {
            if (static_cast<int>(i) == excludeBin) continue;
            sorted[idx++] = magnitude[i];
        }
        sorted.resize(idx);
        std::sort(sorted.begin(), sorted.end());
        return sorted[idx / 2] * 2.0f; // 2x median as noise floor
    }

    float findPeak(const std::vector<float>& magnitude, int centerBin, int radius)
    {
        float peak = 0.0f;
        const int start = std::max(1, centerBin - radius);
        const int end = std::min(static_cast<int>(magnitude.size()) - 1, centerBin + radius);
        for (int i = start; i <= end; ++i)
            if (magnitude[i] > peak) peak = magnitude[i];
        return peak;
    }

    double m_sampleRate = 44100.0;
    int m_fftOrder = 14;
    int m_fftSize = 16384;

    std::unique_ptr<juce::dsp::FFT> m_fft;
    std::vector<float> m_fftBuffer;
    std::vector<float> m_window;
};
