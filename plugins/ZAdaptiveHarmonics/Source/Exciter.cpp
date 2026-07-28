#include "Exciter.h"

//==============================================================================
void Exciter::prepare (double sampleRate, int blkSize, int channels)
{
    numChannels = channels;
    blockSize = blkSize;

    // 2x oversampling with low-latency half-band polyphase IIR
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, 2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true, true);

    oversampling->initProcessing (static_cast<size_t> (blockSize));

    // Input high-pass at 3.5 kHz (12 dB/oct, 2nd order)
    *hpFilter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, 3500.0f, 0.707f);

    hpFilter.prepare ({ sampleRate, static_cast<juce::uint32> (blockSize),
                        static_cast<juce::uint32> (numChannels) });

    // Working buffer for exciter path
    dryBuffer.setSize (numChannels, blockSize, false, true);
    dryBuffer.clear();

    reset();
}

void Exciter::reset()
{
    if (oversampling)
        oversampling->reset();
    hpFilter.reset();
    dryBuffer.clear();
}

int Exciter::getLatencySamples() const noexcept
{
    if (oversampling)
        return static_cast<int> (oversampling->getLatencyInSamples());
    return 0;
}

//==============================================================================
void Exciter::process (juce::AudioBuffer<float>& buffer,
                       float exciterAmount, float exciterEnabled)
{
    if (! oversampling)
        return;

    // True bypass when disabled
    if (exciterEnabled <= 0.0f || exciterAmount <= 0.001f)
        return;

    const auto numSamples = buffer.getNumSamples();
    exciterAmount = juce::jlimit (0.0f, 1.0f, exciterAmount);

    // Save dry signal (we'll mix wet back in)
    dryBuffer.makeCopyOf (buffer, true);

    // Apply input high-pass filter (3.5 kHz) to isolate high frequencies
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> hpContext (block);
        hpFilter.process (hpContext);
    }

    // 2x oversample the high-passed signal
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::AudioBlock<float> osBlock = oversampling->processSamplesUp (block);

    const auto osNumSamples = static_cast<int> (osBlock.getNumSamples());
    const auto osNumChannels = static_cast<int> (osBlock.getNumChannels());

    // Polynomial waveshaper: out = x + k*x^3 (generates odd harmonics of high frequencies)
    // Scale k so that full exciterAmount produces gentle harmonic enhancement
    const float drive = exciterAmount * 1.2f;

    for (int ch = 0; ch < osNumChannels; ++ch)
    {
        auto* data = osBlock.getChannelPointer (static_cast<size_t> (ch));
        for (int i = 0; i < osNumSamples; ++i)
        {
            const float x = data[i];
            // Soft clipper polynomial: tanh-like but cheaper
            data[i] = x + drive * x * x * x;
        }
    }

    // Downsample back
    oversampling->processSamplesDown (block);

    // Mix: dry + exciterAmount * wet (crossfade)
    for (int ch = 0; ch < juce::jmin (numChannels, buffer.getNumChannels()); ++ch)
    {
        auto* wetData = buffer.getWritePointer (ch);
        const auto* dryData = dryBuffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            wetData[i] = dryData[i] + exciterAmount * wetData[i];
        }
    }
}
