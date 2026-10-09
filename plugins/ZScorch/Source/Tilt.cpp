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
    m_cachedTiltDb = std::numeric_limits<float>::infinity();  // force recompute
}

//==============================================================================
void Tilt::updateCoefficients (float tiltDb)
{
    tiltDb = juce::jlimit (-6.0f, 6.0f, tiltDb);

    // Cache: avoid heap allocation on audio thread when tilt hasn't changed
    if (tiltDb == m_cachedTiltDb)
        return;
    m_cachedTiltDb = tiltDb;

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

    // Sanitise hostile input (NaN/Inf) before it touches the filter state.
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            if (! (d[i] > -1.0e30f && d[i] < 1.0e30f))
                d[i] = 0.0f;
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    lowShelf.process (context);
    highShelf.process (context);
}
