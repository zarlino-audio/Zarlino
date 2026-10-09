#include "Exciter.h"

//==============================================================================
// ADAA for the cubic shaper  f(x) = x + k*x^3   ->   F(x) = x^2/2 + k*x^4/4
// (exact antiderivative -> first-order antiderivative anti-aliasing is cheap
// and needs no transcendentals).
//==============================================================================
namespace
{
    inline float cubicF (float x, float k) noexcept
    {
        return x + k * x * x * x;
    }

    inline float cubicF1 (float x, float k) noexcept
    {
        const float x2 = x * x;
        return 0.5f * x2 + 0.25f * k * x2 * x2;
    }

    // Smooth knee bound: unity below `knee`, slope to zero at `end`, constant after.
    // C1-continuous by construction (quadratic blend), cheap, no transcendentals.
    inline float softKnee (float x, float knee, float end) noexcept
    {
        const float a = std::abs (x);
        if (a <= knee)
            return x;

        const float span = end - knee;
        const float over = a - knee;
        const float sign = x < 0.0f ? -1.0f : 1.0f;

        if (over >= span)
            return sign * (knee + 0.5f * span);

        return sign * (a - (over * over) / (2.0f * span));
    }
}

//==============================================================================
void Exciter::prepare (double sampleRate, int blkSize, int channels)
{
    numChannels = channels;
    blockSize = blkSize;

    *hpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 3500.0f, 0.707f);

    hpFilter.prepare ({ sampleRate,
                        static_cast<juce::uint32> (juce::jmax (1, blkSize)),
                        static_cast<juce::uint32> (juce::jmax (1, channels)) });

    dryBuffer.setSize (juce::jmax (1, channels), juce::jmax (1, blkSize), false, true);
    dryBuffer.clear();

    reset();
}

void Exciter::reset()
{
    hpFilter.reset();
    dryBuffer.clear();
    for (auto& p : prevIn)
        p = 0.0f;
}

//==============================================================================
void Exciter::process (juce::AudioBuffer<float>& buffer,
                       float exciterAmount, float exciterEnabled)
{
    // True bypass when disabled — bit-exact passthrough.
    if (exciterEnabled <= 0.0f || exciterAmount <= 0.001f)
    {
        for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), kMaxChannels); ++ch)
            prevIn[ch] = buffer.getSample (ch, juce::jmax (0, buffer.getNumSamples() - 1));
        return;
    }

    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (buffer.getNumChannels(), kMaxChannels, numChannels > 0 ? numChannels : buffer.getNumChannels());

    if (n <= 0 || chans <= 0)
        return;

    exciterAmount = juce::jlimit (0.0f, 1.0f, exciterAmount);
    const float k = 0.7f * exciterAmount;
    const float wetGain = 0.5f * exciterAmount;

    // Sanitise hostile input (NaN/Inf) before it touches the filter state.
    for (int ch = 0; ch < chans; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            if (! (d[i] > -1.0e30f && d[i] < 1.0e30f))
                d[i] = 0.0f;
    }

    // Keep the dry signal for the parallel mix.
    dryBuffer.makeCopyOf (buffer, true);

    // Isolate the high frequencies that will be excited.
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> hpContext (block);
        hpFilter.process (hpContext);
    }

    for (int ch = 0; ch < chans; ++ch)
    {
        auto* wet = buffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (ch);
        float prev = prevIn[ch];

        for (int i = 0; i < n; ++i)
        {
            const float x = wet[i];
            const float delta = x - prev;

            float h;
            if (std::abs (delta) > 1.0e-4f)
                h = (cubicF1 (x, k) - cubicF1 (prev, k)) / delta;
            else
                h = cubicF (0.5f * (x + prev), k);

            // Bound lone transients smoothly (unity below 0.8, hard stop at ~2.05).
            h = softKnee (h, 0.8f, 2.05f);

            wet[i] = dry[i] + wetGain * h;
            prev = x;
        }

        prevIn[ch] = prev;
    }
}
