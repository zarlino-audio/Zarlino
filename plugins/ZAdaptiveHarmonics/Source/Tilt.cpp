#include "Tilt.h"

//==============================================================================
void Tilt::prepare (double sr, int blockSize, int numChannels)
{
    sampleRate = sr;

    // Start with flat response (0 dB tilt)
    *lowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate, 1000.0f, 0.707f, 1.0f);
    *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 1000.0f, 0.707f, 1.0f);

    juce::dsp::ProcessSpec spec { sampleRate,
                                   static_cast<juce::uint32> (blockSize),
                                   static_cast<juce::uint32> (numChannels) };
    lowShelf.prepare (spec);
    highShelf.prepare (spec);

    reset();
}

void Tilt::reset()
{
    lowShelf.reset();
    highShelf.reset();
}

//==============================================================================
void Tilt::updateCoefficients (float tiltDb)
{
    tiltDb = juce::jlimit (-6.0f, 6.0f, tiltDb);

    // Low shelf: ±3 dB gain (half the total tilt)
    // Positive tilt → treble-heavy: low shelf cuts, high shelf boosts
    const float lowGainDb  = -tiltDb * 0.5f;
    const float highGainDb =  tiltDb * 0.5f;

    *lowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sampleRate, 1000.0, 0.707f, juce::Decibels::decibelsToGain (lowGainDb));

    *highShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, 1000.0, 0.707f, juce::Decibels::decibelsToGain (highGainDb));
}

//==============================================================================
void Tilt::process (juce::AudioBuffer<float>& buffer, float tiltDb)
{
    updateCoefficients (tiltDb);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    lowShelf.process (context);
    highShelf.process (context);
}
