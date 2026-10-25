#pragma once

#include "ModuleSupport.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace zfactory
{

//==============================================================================
// Shared building blocks for the modulation family (chorus, flanger, tremolo).
// Header-only and realtime safe: no allocation in any per-sample call.
//==============================================================================

/** A free-running low-frequency oscillator with selectable shape.

    Phase is normalised 0..1 so a second voice can be an exact fraction of a
    cycle behind the first, which is what makes a chorus sound wide instead of
    doubled. */
class ModLfo
{
public:
    void prepare (double sampleRate) noexcept
    {
        m_sampleRate = static_cast<float> (sampleRate);
        m_phase = 0.0f;
    }

    void setRate (float hz) noexcept
    {
        m_increment = std::max (0.0f, hz) / std::max (1.0f, m_sampleRate);
    }

    void reset (float phase = 0.0f) noexcept { m_phase = phase - std::floor (phase); }

    /** Advance one sample and return the bipolar value for the chosen shape. */
    float next (int shape) noexcept
    {
        const float value = evaluate (m_phase, shape);
        m_phase += m_increment;

        if (m_phase >= 1.0f)
            m_phase -= std::floor (m_phase);

        return value;
    }

    static float evaluate (float phase, int shape) noexcept
    {
        switch (shape)
        {
            case 1: // triangle
                return 4.0f * std::abs (phase - 0.5f) - 1.0f;
            case 2: // square
                return phase < 0.5f ? 1.0f : -1.0f;
            default: // sine
                return std::sin (phase * 6.283185307179586f);
        }
    }

private:
    float m_sampleRate = 48000.0f;
    float m_phase = 0.0f;
    float m_increment = 0.0f;
};

/** A delay line read at a fractional position with linear interpolation.

    Linear interpolation is deliberate: it is cheap, allocation-free and, at
    modulation depths above a few milliseconds, inaudible. Oversampled
    interpolation would buy nothing a chorus could use. */
class FractionalDelayLine
{
public:
    void resize (int length) { m_buffer.assign (static_cast<size_t> (std::max (2, length)), 0.0f); m_writeIndex = 0; }

    void clear() noexcept
    {
        std::fill (m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writeIndex = 0;
    }

    int length() const noexcept { return static_cast<int> (m_buffer.size()); }

    void write (float value) noexcept
    {
        if (m_buffer.empty())
            return;

        m_buffer[static_cast<size_t> (m_writeIndex)] = flushDenormal (value);

        if (++m_writeIndex >= static_cast<int> (m_buffer.size()))
            m_writeIndex = 0;
    }

    float read (float delaySamples) const noexcept
    {
        const int length = static_cast<int> (m_buffer.size());

        if (length <= 1)
            return 0.0f;

        const float clamped = std::max (1.0f, std::min (delaySamples, static_cast<float> (length - 2)));
        float position = static_cast<float> (m_writeIndex) - clamped;

        while (position < 0.0f)
            position += static_cast<float> (length);

        const int i0 = static_cast<int> (position) % length;
        const int i1 = (i0 + 1) % length;
        const float fraction = position - std::floor (position);
        const float a = m_buffer[static_cast<size_t> (i0)];
        const float b = m_buffer[static_cast<size_t> (i1)];

        return a + (b - a) * fraction;
    }

private:
    std::vector<float> m_buffer;
    int m_writeIndex = 0;
};

} // namespace zfactory
