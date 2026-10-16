#pragma once

#include "zfactory/DspModule.h"

#include "ZDspUtils.h"
#include "ZSmoother.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

//==============================================================================
// Helpers shared by the factory module library. Everything here is header-only
// and realtime safe: no allocation, no locks, no I/O.
//==============================================================================

/** Replace NaN and +/-Inf with 0.0f in place.
    Hostile input (a denormalised file, a broken upstream plugin, a DAW glitch)
    must never be allowed to poison a filter memory or an envelope follower and
    then spread through the rest of the chain. The factory test suite feeds
    NaN/Inf blocks into every module and asserts the output stays finite. */
inline void sanitiseBlock (float* const* channels, int numChannels, int numSamples) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = channels[ch];

        if (data == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float value = data[i];

            if (! (value > -1.0e30f && value < 1.0e30f))
                data[i] = 0.0f;
        }
    }
}

/** Reject subnormal magnitudes so IIR feedback paths cannot fall into the
    x86/x64 subnormal range where microcode assists make the CPU 10-100x
    slower. Same policy as ZEnvelopeFollower / ZEqualizer. */
inline float flushDenormal (float value) noexcept
{
    if (value < 1.0e-30f && value > -1.0e-30f)
        return 0.0f;

    return value;
}

/** Clamp to 0..1, treating NaN as 0. */
inline float clamp01 (float value) noexcept
{
    if (! (value == value))
        return 0.0f;

    return std::min (1.0f, std::max (0.0f, value));
}

//==============================================================================
/** A parameter that ramps linearly from its current value to its target over a
    fixed time, advanced once per sample. Wraps ZLinearSmoother so that every
    module in the library uses the same, already-tested ramp implementation. */
class SmoothedParameter
{
public:
    SmoothedParameter() = default;

    void prepare (double sampleRate, double rampSeconds, float initialValue)
    {
        m_smoother.reset (sampleRate, rampSeconds);
        m_current = initialValue;
        m_target = initialValue;
        m_smoother.setCurrentAndTargetValue (initialValue);
    }

    void setTarget (float value) noexcept
    {
        if (value == m_target)
            return;

        m_target = value;
        m_smoother.setTargetValue (value);
    }

    void setImmediate (float value) noexcept
    {
        m_current = value;
        m_target = value;
        m_smoother.setCurrentAndTargetValue (value);
    }

    float next() noexcept
    {
        m_current = m_smoother.getNextValue();
        return m_current;
    }

    /** Advance by a whole block without reading per-sample values (used by
        controllers that only need block-rate resolution). */
    void skip (int numSamples) noexcept { m_smoother.skip (numSamples); }

    float current() const noexcept { return m_current; }
    float target() const noexcept { return m_target; }

    void reset (float value) noexcept { setImmediate (value); }

private:
    ZLinearSmoother m_smoother;
    float m_current = 0.0f;
    float m_target = 0.0f;
};

} // namespace zfactory
