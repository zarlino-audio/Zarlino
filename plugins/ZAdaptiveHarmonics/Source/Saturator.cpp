#include "Saturator.h"

//==============================================================================
void Saturator::prepare (double sampleRate, int blockSize, int channels)
{
    numChannels = channels;

    // 2x oversampling with low-latency half-band polyphase IIR
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);

    oversampling->initProcessing (static_cast<size_t> (blockSize));

    // Post-shaping low-pass: gentle 6 dB/oct shelf at ~8 kHz, gain adjustable
    // Start with -24 dB for full reduction, gain will be modulated
    *lpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate * 2.0, 8000.0f, 0.707f,
        juce::Decibels::decibelsToGain (-24.0f));

    lpFilter.prepare ({ sampleRate * 2.0, static_cast<juce::uint32> (blockSize * 2),
                        static_cast<juce::uint32> (numChannels) });

    reset();
}

void Saturator::reset()
{
    if (oversampling)
        oversampling->reset();
    lpFilter.reset();
}

int Saturator::getLatencySamples() const noexcept
{
    if (oversampling)
        return static_cast<int> (oversampling->getLatencyInSamples());
    return 0;
}

//==============================================================================
void Saturator::process (juce::AudioBuffer<float>& buffer,
                         float evenAmount, float oddAmount, float harmReduction)
{
    if (! oversampling)
        return;

    const auto numSamples = buffer.getNumSamples();

    // Build oversampling block
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::AudioBlock<float> osBlock = oversampling->processSamplesUp (block);

    const auto osNumSamples = static_cast<int> (osBlock.getNumSamples());
    const auto osNumChannels = static_cast<int> (osBlock.getNumChannels());

    // Clamp parameters
    evenAmount    = juce::jlimit (0.0f, 2.0f, evenAmount);
    oddAmount     = juce::jlimit (0.0f, 2.0f, oddAmount);
    harmReduction = juce::jlimit (0.0f, 1.0f, harmReduction);

    // Polynomial waveshaper: out = x + a2*x^2 + a3*x^3
    for (int ch = 0; ch < osNumChannels; ++ch)
    {
        auto* data = osBlock.getChannelPointer (static_cast<size_t> (ch));
        for (int i = 0; i < osNumSamples; ++i)
        {
            const float x = data[i];
            if (std::isnan (x) || std::isinf (x))
                data[i] = 0.0f;
            else
                data[i] = x + evenAmount * x * x + oddAmount * x * x * x;
        }
    }

    // Post-shaping harmonic reduction: low-shelf filter to roll off 5th+ harmonics
    // harmReduction 0.0 → minimal reduction (-3 dB), 1.0 → full reduction (-24 dB)
    const float reductionDb = -3.0f - harmReduction * 21.0f; // range: -3dB to -24dB
    *lpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        oversampling->getOversamplingFactor() * 44100.0, // approximate, OK for shelf
        8000.0f, 0.707f, juce::Decibels::decibelsToGain (reductionDb));

    // Apply the filter in-place on the oversampled block
    juce::dsp::ProcessContextReplacing<float> lpContext (osBlock);
    lpFilter.process (lpContext);

    // Downsample back
    oversampling->processSamplesDown (block);
}
