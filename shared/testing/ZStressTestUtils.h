#pragma once

#include <JuceHeader.h>
#include <random>

// Utility helpers for stress/fuzz testing audio processors
namespace ZStressTestUtils
{
    inline void generateImpulse(juce::AudioBuffer<float>& buf, int channel, int position, float amplitude = 1.0f)
    {
        buf.clear();
        if (channel < 0 || channel >= buf.getNumChannels())
            return;
        if (position < 0 || position >= buf.getNumSamples())
            return;
        buf.setSample(channel, position, amplitude);
    }

    inline void generateSine(juce::AudioBuffer<float>& buf, int channel, double frequency, double sampleRate, float amplitude = 0.5f)
    {
        if (channel < 0 || channel >= buf.getNumChannels())
            return;
        const int N = buf.getNumSamples();
        const double twoPiF = juce::MathConstants<double>::twoPi * frequency;
        for (int n = 0; n < N; ++n)
        {
            const float v = static_cast<float>(amplitude * std::sin(twoPiF * (static_cast<double>(n) / sampleRate)));
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample(ch, n, (ch == channel) ? v : buf.getSample(ch, n));
        }
    }

    inline void generateWhiteNoise(juce::AudioBuffer<float>& buf, int channel, float amplitude = 0.5f, uint32_t seed = 0xC0FFEE)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(-amplitude, amplitude);
        for (int n = 0; n < buf.getNumSamples(); ++n)
        {
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample(ch, n, (ch == channel) ? d(rng) : buf.getSample(ch, n));
        }
    }

    inline void generateNaNBuffer(juce::AudioBuffer<float>& buf)
    {
        const int chans = buf.getNumChannels();
        const int N = buf.getNumSamples();
        for (int ch = 0; ch < chans; ++ch)
            for (int n = 0; n < N; ++n)
                buf.setSample(ch, n, std::numeric_limits<float>::quiet_NaN());
    }

    inline void generateInfBuffer(juce::AudioBuffer<float>& buf)
    {
        const int chans = buf.getNumChannels();
        const int N = buf.getNumSamples();
        for (int ch = 0; ch < chans; ++ch)
            for (int n = 0; n < N; ++n)
                buf.setSample(ch, n, (n % 2 == 0) ? std::numeric_limits<float>::infinity()
                                                  : -std::numeric_limits<float>::infinity());
    }

    inline void generateDenormalBuffer(juce::AudioBuffer<float>& buf, float value = 1e-40f)
    {
        const int chans = buf.getNumChannels();
        const int N = buf.getNumSamples();
        for (int ch = 0; ch < chans; ++ch)
            for (int n = 0; n < N; ++n)
                buf.setSample(ch, n, value * ((n % 2) ? 1.0f : -1.0f));
    }

    // Rapidly change all parameters on a processor (uses AudioProcessor::getParameters())
    inline void generateRapidAutomation(juce::AudioProcessor& proc, int numSteps, juce::Random* rng = nullptr)
    {
        juce::Random localRng;
        juce::Random& r = rng ? *rng : localRng;

        auto& params = proc.getParameters();
        const int P = static_cast<int>(params.size());
        if (P == 0) return;

        for (int step = 0; step < numSteps; ++step)
        {
            for (int pi = 0; pi < P; ++pi)
            {
                if (auto* p = params[pi])
                {
                    // Only attempt to set if the parameter supports ranges via setValue
                    const float v = r.nextFloat();
                    p->setValueNotifyingHost(v);
                }
            }
        }
    }

    // Sweep sample rates: call prepareToPlay and process a short buffer to exercise code paths
    inline void sweepSampleRate(juce::AudioProcessor& proc, const std::vector<double>& rates, int blockSize = 512)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf(proc.getTotalNumOutputChannels(), blockSize);
        for (auto sr : rates)
        {
            proc.releaseResources();
            proc.prepareToPlay(sr, blockSize);
            buf.clear();
            proc.processBlock(buf, midi);
        }
    }

    // Sweep block sizes: reprepare with different block sizes and process
    inline void sweepBlockSize(juce::AudioProcessor& proc, const std::vector<int>& sizes, double sampleRate = 44100.0)
    {
        juce::MidiBuffer midi;
        for (auto bs : sizes)
        {
            proc.releaseResources();
            proc.prepareToPlay(sampleRate, bs);
            juce::AudioBuffer<float> buf(proc.getTotalNumOutputChannels(), bs);
            buf.clear();
            proc.processBlock(buf, midi);
        }
    }

} // namespace ZStressTestUtils
