#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Dynamic Spectral Tilt — a pair of complementary shelving filters that shift
    the spectral balance from bass-emphasized to treble-emphasized.

    Design:
      - Low shelf (±3 dB below 1 kHz pivot)
      - High shelf (∓3 dB above 1 kHz pivot)
      - Net tilt: ±6 dB range
      - Minimum-phase IIR, no additional latency
*/
class Tilt
{
public:
    Tilt() = default;

    /** Compute initial filter coefficients. Call in prepareToPlay. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /** Reset filter state. */
    void reset();

    /**
        Process a buffer of audio in-place.
        @param buffer     Audio buffer to process.
        @param tiltDb     Tilt amount in dB: negative = bass-heavy, positive = treble-heavy.
                          Range [-6.0, +6.0].
    */
    void process (juce::AudioBuffer<float>& buffer, float tiltDb);

private:
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> lowShelf;
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> highShelf;
    double sampleRate = 44100.0;

    void updateCoefficients (float tiltDb);
};
