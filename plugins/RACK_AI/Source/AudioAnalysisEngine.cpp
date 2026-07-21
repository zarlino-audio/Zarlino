#include "AudioAnalysisEngine.h"
#include <cmath>

AudioAnalysisEngine::AudioAnalysisEngine()
{
}

void AudioAnalysisEngine::prepare (double sampleRate, int blockSize)
{
    mSampleRate = sampleRate;
    mBlockSize = blockSize;
    mFFTSize = 4096;
    mFFT = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (mFFTSize)));
    mFFTBuffer.resize (mFFTSize * 2, 0.0f);
    mWindow.resize (mFFTSize);
    
    // Create Hann window
    for (int i = 0; i < mFFTSize; ++i)
        mWindow[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / (mFFTSize - 1)));
}

void AudioAnalysisEngine::analyzeAudio (const juce::AudioBuffer<float>& buffer)
{
    juce::ScopedLock lock (mAnalysisLock);
    
    AnalysisSnapshot snapshot;
    snapshot.timestamp = juce::Time::getCurrentTime().toMilliseconds();
    
    computeFFT (buffer);
    computeDynamics (buffer);
    computeHarmonics (buffer);
    computeStereo (buffer);
    computeTimeDomain (buffer);
    
    mAnalysisHistory.push_back (snapshot);
    if (mAnalysisHistory.size() > 100)
        mAnalysisHistory.pop_front();
}

void AudioAnalysisEngine::computeFFT (const juce::AudioBuffer<float>& buffer)
{
    // Simplified FFT computation
    // Full implementation would compute 1/3 octave bands
}

void AudioAnalysisEngine::computeDynamics (const juce::AudioBuffer<float>& buffer)
{
    // Compute RMS, peak, crest factor
}

void AudioAnalysisEngine::computeHarmonics (const juce::AudioBuffer<float>& buffer)
{
    // Compute fundamental, harmonic-to-noise ratio
}

void AudioAnalysisEngine::computeStereo (const juce::AudioBuffer<float>& buffer)
{
    // Compute stereo width, phase coherence
}

void AudioAnalysisEngine::computeTimeDomain (const juce::AudioBuffer<float>& buffer)
{
    // Compute zero-crossing rate, signal density
}

AnalysisSnapshot AudioAnalysisEngine::getLatestSnapshot() const
{
    juce::ScopedLock lock (mAnalysisLock);
    if (mAnalysisHistory.empty())
        return AnalysisSnapshot();
    return mAnalysisHistory.back();
}

std::vector<AnalysisSnapshot> AudioAnalysisEngine::getAnalysisHistory (int maxEntries) const
{
    juce::ScopedLock lock (mAnalysisLock);
    std::vector<AnalysisSnapshot> result;
    int start = std::max (0, static_cast<int> (mAnalysisHistory.size()) - maxEntries);
    for (int i = start; i < static_cast<int> (mAnalysisHistory.size()); ++i)
        result.push_back (mAnalysisHistory[i]);
    return result;
}
