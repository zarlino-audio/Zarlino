#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Exciter — adds high-frequency harmonics with a smooth high-pass gate.

    v2.0 signal path:
        input → 12 dB/oct high-pass @ 3.5 kHz → ADAA cubic shaper (x + k*x^3)
              → soft bound → mix back with the dry signal.

    Compared with the previous 2x-oversampled design this path has:
      * zero latency (no oversampling filters),
      * a fraction of the CPU (the cubic shaper has an analytic double
        antiderivative, so antiderivative anti-aliasing is nearly free),
      * a bounded output (the soft bound keeps lone transients from spiking).

    The module is gated: when `exciterEnabled` is 0 (or the amount is ~0), audio
    passes through bit-exactly.
*/
class Exciter
{
public:
    Exciter() = default;

    /** Prepare filters and buffers. Call in prepareToPlay. */
    void prepare (double sampleRate, int blockSize, int numChannels);

    /** Reset filter/ADAA state. */
    void reset();

    /** Latency introduced by this module (always 0 in v2.0). */
    int getLatencySamples() const noexcept { return 0; }

    /**
        Process a buffer of audio in-place.
        @param buffer         Audio buffer to process.
        @param exciterAmount  Drive amount (0.0–1.0) for the harmonics.
        @param exciterEnabled Gates the module: 0 = bypass, 1 = active.
    */
    void process (juce::AudioBuffer<float>& buffer,
                  float exciterAmount, float exciterEnabled);

private:
    static constexpr int kMaxChannels = 8;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> hpFilter;

    juce::AudioBuffer<float> dryBuffer;
    float prevIn[kMaxChannels] = {};   // ADAA history (HP-filtered signal)
    int numChannels = 0;
    int blockSize = 0;
};
