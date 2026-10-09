#pragma once

#include <cstddef>

// Linear interpolating smoother — approaches target at a constant rate
class ZLinearSmoother {
public:
    ZLinearSmoother() = default;

    // Set up the smoothing rate
    // sampleRate: audio sample rate in Hz
    // rampTimeSeconds: time to go from 0 to 1 (50ms = 0.05)
    void reset(double sampleRate, double rampTimeSeconds)
    {
        // Number of samples over which to go from 0 to 1
        double rampSamples = sampleRate * rampTimeSeconds;
        if (rampSamples <= 0.0)
            m_step = 1.0f; // instant
        else
            m_step = static_cast<float>(1.0 / rampSamples);
    }

    // Jump to value immediately (no smoothing)
    void setCurrentAndTargetValue(float newValue)
    {
        m_current = newValue;
        m_target = newValue;
        m_isConstant = true;
    }

    // Set a new target — smoother will approach it over time
    void setTargetValue(float targetValue)
    {
        m_target = targetValue;
        m_isConstant = (m_current == m_target);
        if (m_isConstant)
            return;
        // keep m_step as configured by reset(); do not change here
    }

    // Read current value without advancing
    float getCurrentValue() const noexcept { return m_current; }

    // Advance one step toward target and return new value
    float getNextValue()
    {
        if (m_isConstant)
            return m_current;

        if (m_current < m_target) {
            m_current += m_step;
            if (m_current >= m_target) {
                m_current = m_target;
                m_isConstant = true;
            }
        } else {
            m_current -= m_step;
            if (m_current <= m_target) {
                m_current = m_target;
                m_isConstant = true;
            }
        }
        return m_current;
    }

    // Advance N steps toward target without returning values
    void skip(int numSamples)
    {
        if (m_isConstant || numSamples <= 0)
            return;

        float delta = m_step * static_cast<float>(numSamples);
        if (m_current < m_target) {
            m_current += delta;
            if (m_current >= m_target) {
                m_current = m_target;
                m_isConstant = true;
            }
        } else {
            m_current -= delta;
            if (m_current <= m_target) {
                m_current = m_target;
                m_isConstant = true;
            }
        }
    }

private:
    float m_current = 0.0f;
    float m_target = 0.0f;
    float m_step = 0.0f;      // per-sample step size (positive)
    bool m_isConstant = true;  // true when already at target
};
