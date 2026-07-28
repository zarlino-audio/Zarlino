#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Normalized spectral features produced by the AnalysisCore.
    All values are in [0.0, 1.0].
*/
struct ControlVector
{
    float harmonicDensity  = 0.0f;  // mid-band energy proxy
    float spectralFlatness = 0.0f;  // tonal (low) vs noisy (high)
    float crestFactor      = 0.0f;  // peak/RMS ratio
    float brightness       = 0.0f;  // high-band energy ratio
    float activity         = 0.0f;  // overall signal energy, for metering
};

//==============================================================================
/**
    Shared Analysis Core — computes spectral features from the input signal
    at control rate (~100-200 Hz) using FFT-based multi-band analysis.

    No lookahead — uses only the current block's data.
    All buffers allocated in prepare(). Safe to call process() from audio thread.
*/
class AnalysisCore
{
public:
    AnalysisCore() = default;

    /** Allocate FFT and working buffers. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Release resources. */
    void reset();

    /**
        Process a block of samples and update the control vector.
        Should be called periodically (every 256-512 samples) at control rate.
    */
    void process (const juce::AudioBuffer<float>& buffer,
                  int numSamples, int numChannels);

    /** The most recently computed control vector. */
    const ControlVector& getControlVector() const noexcept { return cv; }

private:
    enum { kFFTOrder = 10, kFFTSize = 1 << kFFTOrder }; // 1024-point FFT

    std::unique_ptr<juce::dsp::FFT> fft;
    juce::AudioBuffer<float> fftBuffer;     // accumulated time-domain data
    int fftWritePos = 0;
    int fftCountdown = 0;

    ControlVector cv;

    // Frequency band edges (log-spaced, 8 bands, 20 Hz – 20 kHz)
    static constexpr int kNumBands = 8;
    int bandBins[kNumBands + 1] = {};   // FFT bin indices for each band edge

    void computeBands (double sampleRate);
    void updateControlVector (const float* spectrum, int numChannels,
                              const juce::AudioBuffer<float>& buffer,
                              int numSamples);
};
