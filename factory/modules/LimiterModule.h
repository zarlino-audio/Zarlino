#pragma once

#include "ModuleSupport.h"

#include "ZDynamics.h"

#include <array>

namespace zfactory
{

//==============================================================================
/** zarlino.limiter - brick-wall lookahead limiter.

    Wraps ZLimiter (shared/dsp). The lookahead window is a fixed 1 ms, so the
    reported latency is exactly one millisecond expressed in samples and is
    known analytically for every supported rate. */
class LimiterModule final : public DspModule
{
public:
    LimiterModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return m_latencySamples; }

    static constexpr int kMaxChannels = 2;
    static constexpr float kLookaheadMs = 1.0f;

    enum ParameterIndex
    {
        Ceiling = 0,
        Release,
        NumParameters
    };

private:
    void applyParameters() noexcept;

    ZLimiter m_limiter;                  // ZLimiter is multi-channel aware
    float m_values[NumParameters] {};
    int m_latencySamples = 0;
    int m_appliedChannels = 0;
    bool m_dirty = true;
};

} // namespace zfactory
