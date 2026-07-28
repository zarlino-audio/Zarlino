#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Exciter — generates high-frequency harmonics only, with a hard high-pass
    input gate at ~3.5 kHz.

    Signal path:
      input → high-pass 3.5 kHz → 2x upsample → polynomial waveshaper
      (x + k*x^3) → 2x downsample → mix back with dry signal at controlled level.

    The module is gated by `exciterEnabled`: when 0, audio passes through unchanged.
*/
class Exciter
{
public:
    Exciter() = default;

    /** Create oversampling and filters. Call in prepareToPlay. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /** Release resources. */
    void reset();

    /** Returns the latency in samples introduced by oversampling. */
    int getLatencySamples() const noexcept;

    /**
        Process a buffer of audio in-place.
        @param buffer         Audio buffer to process.
        @param exciterAmount  Drive amount (0.0–1.0) for the harmonics.
        @param exciterEnabled Gates the module: 0 = bypass, 1 = active.
    */
    void process (juce::AudioBuffer<float>& buffer,
                  float exciterAmount, float exciterEnabled);

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> hpFilter;

    juce::AudioBuffer<float> dryBuffer;
    int numChannels = 0;
    int blockSize = 0;
};
