#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Saturator — generates 2nd/3rd harmonic content via asymmetric polynomial
    waveshaping with 2x oversampling and post-shaping harmonic reduction.

    Signal path: input → 2x upsample → polynomial waveshaper → low-pass shelf
    (harm reduction) → 2x downsample → output.

    Latency from oversampling is reported via getLatencySamples().
*/
class Saturator
{
public:
    Saturator() = default;

    /** Create the oversampling object and filter. Call in prepareToPlay. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /** Release resources. */
    void reset();

    /** Returns the latency in samples introduced by oversampling. */
    int getLatencySamples() const noexcept;

    /**
        Process a buffer of audio in-place.
        @param buffer    Audio buffer to process (all channels).
        @param evenAmount  2nd harmonic coefficient a2 (0.0–~1.5).
        @param oddAmount   3rd harmonic coefficient a3 (0.0–~0.78).
        @param harmReduction  Post-shaping high-frequency roll-off (0.0–1.0).
    */
    void process (juce::AudioBuffer<float>& buffer,
                  float evenAmount, float oddAmount, float harmReduction);

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> lpFilter;

    int numChannels = 0;
};
