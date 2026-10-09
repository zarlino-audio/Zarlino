#pragma once

#include <vector>
#include <cmath>
#include <cassert>

static constexpr float ZB_PI = 3.14159265358979323846f;

// RBJ cookbook biquad implementation — header-only, no JUCE dependency
// Designed for single-precision float audio processing

struct BiquadCoefficients {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f; // a0 is normalized to 1.0f
};

namespace RBJ {

static inline BiquadCoefficients makeLowShelf(float sampleRate, float freqHz, float Q, float gainLinear)
{
    assert(sampleRate > 0.0f);
    BiquadCoefficients c;

    const float A = std::sqrt(gainLinear);
    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float tmp = (A * A + 1.0f) / Q - (A - 1.0f) * (A - 1.0f);
    const float beta = sin_w * std::sqrt(std::max(0.0f, tmp));

    const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w + beta);
    const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w);
    const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w - beta);

    const float a0 = (A + 1.0f) + (A - 1.0f) * cos_w + beta;
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w);
    const float a2 = (A + 1.0f) + (A - 1.0f) * cos_w - beta;

    // normalize
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

static inline BiquadCoefficients makeHighShelf(float sampleRate, float freqHz, float Q, float gainLinear)
{
    assert(sampleRate > 0.0f);
    BiquadCoefficients c;

    const float A = std::sqrt(gainLinear);
    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float tmp = (A * A + 1.0f) / Q - (A - 1.0f) * (A - 1.0f);
    const float beta = sin_w * std::sqrt(std::max(0.0f, tmp));

    const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w + beta);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w);
    const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w - beta);

    const float a0 = (A + 1.0f) - (A - 1.0f) * cos_w + beta;
    const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w);
    const float a2 = (A + 1.0f) - (A - 1.0f) * cos_w - beta;

    // normalize
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

static inline BiquadCoefficients makeHighPass(float sampleRate, float freqHz, float Q)
{
    assert(sampleRate > 0.0f);
    BiquadCoefficients c;

    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 = (1.0f + cos_w) / 2.0f;
    const float b1 = -(1.0f + cos_w);
    const float b2 = (1.0f + cos_w) / 2.0f;

    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 = 1.0f - alpha;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

static inline BiquadCoefficients makeLowPass(float sampleRate, float freqHz, float Q)
{
    assert(sampleRate > 0.0f);
    BiquadCoefficients c;

    const float omega = 2.0f * ZB_PI * freqHz / sampleRate;
    const float sin_w = std::sin(omega);
    const float cos_w = std::cos(omega);
    const float alpha = sin_w / (2.0f * Q);

    const float b0 = (1.0f - cos_w) / 2.0f;
    const float b1 = 1.0f - cos_w;
    const float b2 = (1.0f - cos_w) / 2.0f;

    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cos_w;
    const float a2 = 1.0f - alpha;

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    return c;
}

} // namespace RBJ

struct BiquadState {
    // Using DF1 Transposed internal states z1, z2
    float z1 = 0.0f;
    float z2 = 0.0f;

    void reset() { z1 = 0.0f; z2 = 0.0f; }
};

class MultiChannelBiquad {
public:
    MultiChannelBiquad() = default;

    void setCoefficients(const BiquadCoefficients& coeffs)
    {
        m_coeffs = coeffs;
    }

    void prepare(int numChannels)
    {
        if (numChannels < 0)
            return;
        m_states.assign(static_cast<size_t>(numChannels), BiquadState());
    }

    void reset()
    {
        for (auto& s : m_states)
            s.reset();
    }

    void processChannel(int channel, float* samples, int numSamples)
    {
        assert(channel >= 0);
        assert(samples != nullptr || numSamples == 0);
        if (channel < 0 || channel >= static_cast<int>(m_states.size()))
            return; // not prepared for this channel

        BiquadState& st = m_states[static_cast<size_t>(channel)];
        const float b0 = m_coeffs.b0;
        const float b1 = m_coeffs.b1;
        const float b2 = m_coeffs.b2;
        const float a1 = m_coeffs.a1;
        const float a2 = m_coeffs.a2;

        for (int i = 0; i < numSamples; ++i) {
            const float x = samples[i];
            const float y = b0 * x + st.z1;
            const float new_z1 = b1 * x - a1 * y + st.z2;
            const float new_z2 = b2 * x - a2 * y;
            st.z1 = new_z1;
            st.z2 = new_z2;
            samples[i] = y;
        }
    }

    void process(float* const* channelData, int numChannels, int numSamples)
    {
        assert(channelData != nullptr || numChannels == 0 || numSamples == 0);
        // If not prepared, lazily prepare
        if (static_cast<int>(m_states.size()) < numChannels)
            prepare(numChannels);

        for (int ch = 0; ch < numChannels; ++ch) {
            float* chPtr = channelData[ch];
            if (chPtr == nullptr)
                continue;
            processChannel(ch, chPtr, numSamples);
        }
    }

private:
    BiquadCoefficients m_coeffs;
    std::vector<BiquadState> m_states;
};
