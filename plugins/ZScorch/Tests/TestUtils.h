#pragma once

//==============================================================================
// Shared helpers for the ZScorch v2.0 test suites.
//==============================================================================
#include <JuceHeader.h>
#include <cmath>
#include <vector>
#include <random>

#include "PluginProcessor.h"
#include "Saturator.h"

namespace ztest
{
    inline juce::AudioBuffer<float> makeBuffer (int chans, int n)
    {
        juce::AudioBuffer<float> b (juce::jmax (1, chans), juce::jmax (1, n));
        b.clear();
        return b;
    }

    inline void fillSine (juce::AudioBuffer<float>& b, double sr,
                          float freq, float amp, float phase = 0.0f)
    {
        const double w = juce::MathConstants<double>::twoPi * freq / sr;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = amp * (float) std::sin (w * i + phase);
        }
    }

    inline float peak (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            p = juce::jmax (p, b.getMagnitude (ch, 0, b.getNumSamples()));
        return p;
    }

    inline float rms (const juce::AudioBuffer<float>& b)
    {
        double acc = 0.0;
        const int n = b.getNumSamples();
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                acc += (double) d[i] * (double) d[i];
        }
        return (float) std::sqrt (acc / juce::jmax (1, n * b.getNumChannels()));
    }

    inline bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i]))
                    return false;
        }
        return true;
    }

    inline float maxAbsDiff (const juce::AudioBuffer<float>& a,
                             const juce::AudioBuffer<float>& b)
    {
        float m = 0.0f;
        const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
        for (int ch = 0; ch < juce::jmin (a.getNumChannels(), b.getNumChannels()); ++ch)
        {
            const auto* x = a.getReadPointer (ch);
            const auto* y = b.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                m = juce::jmax (m, std::abs (x[i] - y[i]));
        }
        return m;
    }

    /** Blackman-Harris windowed magnitude spectrum (first half), from a mono signal.
        The 4-term window has ~-92 dB sidelobes, so alias measurements are not
        polluted by spectral leakage from the harmonic series itself. */
    inline std::vector<float> spectrum (const float* x, int n, int fftOrder)
    {
        const int fftSize = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);

        std::vector<float> data ((size_t) fftSize * 2, 0.0f);
        const int copy = juce::jmin (n, fftSize);
        for (int i = 0; i < copy; ++i)
        {
            const float phase = juce::MathConstants<float>::twoPi
                              * (float) i / (float) (fftSize - 1);
            const float w = 0.35875f
                          - 0.48829f * std::cos (phase)
                          + 0.14128f * std::cos (2.0f * phase)
                          - 0.01168f * std::cos (3.0f * phase);
            data[(size_t) i] = x[i] * w;
        }

        fft.performFrequencyOnlyForwardTransform (data.data());

        std::vector<float> mag ((size_t) fftSize / 2 + 1);
        for (size_t i = 0; i < mag.size(); ++i)
            mag[i] = data[i];
        return mag;
    }

    inline float binToHz (int bin, int fftOrder, double sr)
    {
        const int fftSize = 1 << fftOrder;
        return (float) ((double) bin * sr / (double) fftSize);
    }

    inline int hzToBin (float hz, int fftOrder, double sr)
    {
        const int fftSize = 1 << fftOrder;
        return juce::roundToInt ((double) hz * (double) fftSize / sr);
    }

    /** Sum of magnitudes in a +/- width window around a bin. */
    inline float bandSum (const std::vector<float>& mag, int centre, int width)
    {
        float s = 0.0f;
        for (int b = juce::jmax (1, centre - width);
             b <= juce::jmin ((int) mag.size() - 1, centre + width); ++b)
            s = juce::jmax (s, mag[(size_t) b]);
        return s;
    }

    //--------------------------------------------------------------------------
    inline void setParam (juce::AudioProcessorValueTreeState& vts,
                          const juce::String& id, float value)
    {
        if (auto* p = vts.getParameter (id))
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (value));
    }

    inline float getParam (juce::AudioProcessorValueTreeState& vts, const juce::String& id)
    {
        if (auto* p = vts.getParameter (id))
            return p->getNormalisableRange().convertFrom0to1 (p->getValue());
        return 0.0f;
    }

    /** Render `input` through the processor (block processing) and return the output. */
    inline juce::AudioBuffer<float> render (ZScorchAudioProcessor& proc,
                                            const juce::AudioBuffer<float>& input,
                                            int blockSize = 512,
                                            int numBlocks = 1)
    {
        const int chans = input.getNumChannels();
        juce::AudioBuffer<float> out (chans, input.getNumSamples() * juce::jmax (1, numBlocks));
        out.clear();

        proc.prepareToPlay (44100.0, blockSize);

        juce::MidiBuffer midi;
        const int total = out.getNumSamples();
        for (int start = 0; start < total; start += blockSize)
        {
            const int n = juce::jmin (blockSize, total - start);
            juce::AudioBuffer<float> block (chans, n);
            block.clear();
            for (int ch = 0; ch < chans; ++ch)
                block.copyFrom (ch, 0, input, ch, start % input.getNumSamples(), n);

            proc.processBlock (block, midi);

            for (int ch = 0; ch < chans; ++ch)
                out.copyFrom (ch, start, block, ch, 0, n);
        }

        return out;
    }
}
