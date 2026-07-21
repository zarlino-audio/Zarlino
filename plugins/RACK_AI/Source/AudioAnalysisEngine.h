#pragma once

#include <JuceHeader.h>
#include <deque>
#include <array>

struct AnalysisSnapshot
{
    uint64_t timestamp = 0;

    // Spectral Analysis
    std::array<float, 31> octaveBandEnergy = {};  // 1/3 octave bands from 20 Hz to 20 kHz
    float spectralCentroid = 0.0f;
    float spectralSpread = 0.0f;
    float spectralFlux = 0.0f;
    float spectralRolloff = 0.0f;

    // Dynamics Analysis
    float rmsShort = 0.0f;      // 100 ms
    float rmsLong = 0.0f;       // 3 s
    float peakLevel = 0.0f;
    float truePeak = 0.0f;
    float crestFactor = 0.0f;
    float dynamicRange = 0.0f;  // LRA
    float transientDensity = 0.0f;
    float lufsI = 0.0f;         // Integrated
    float lufsS = 0.0f;         // Short-term
    float lufsM = 0.0f;         // Momentary
    float attackTime = 0.0f;
    float releaseTime = 0.0f;

    // Harmonic Analysis
    float fundamentalFreq = 0.0f;
    float fundamentalConfidence = 0.0f;
    float harmonicToNoiseRatio = 0.0f;
    float oddEvenBalance = 0.0f;
    float inharmonicity = 0.0f;

    // Stereo Analysis
    float stereoWidth = 0.0f;      // -1 to +1
    float leftRightBalance = 0.0f;  // -1 to +1
    float midSideRatio = 0.0f;
    float phaseCoherence = 0.0f;   // 0 to 1

    // Time-Domain
    float zeroCrossingRate = 0.0f;
    float signalDensity = 0.0f;
    bool hasResonance = false;
};

class AudioAnalysisEngine
{
public:
    AudioAnalysisEngine();
    ~AudioAnalysisEngine() = default;

    void prepare (double sampleRate, int blockSize);
    void analyzeAudio (const juce::AudioBuffer<float>& buffer);
    AnalysisSnapshot getLatestSnapshot() const;
    std::vector<AnalysisSnapshot> getAnalysisHistory (int maxEntries = 10) const;

private:
    void computeFFT (const juce::AudioBuffer<float>& buffer);
    void computeDynamics (const juce::AudioBuffer<float>& buffer);
    void computeHarmonics (const juce::AudioBuffer<float>& buffer);
    void computeStereo (const juce::AudioBuffer<float>& buffer);
    void computeTimeDomain (const juce::AudioBuffer<float>& buffer);

    double mSampleRate = 44100.0;
    int mBlockSize = 512;
    int mFFTSize = 4096;

    std::unique_ptr<juce::dsp::FFT> mFFT;
    std::vector<float> mFFTBuffer;
    std::vector<float> mWindow;

    std::deque<AnalysisSnapshot> mAnalysisHistory;
    juce::CriticalSection mAnalysisLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioAnalysisEngine)
};
