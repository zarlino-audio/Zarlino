#pragma once

#include "ModuleSupport.h"

#include "ZDynamics.h"

#include <array>

namespace zfactory
{

//==============================================================================
/** zarlino.shaping.transient - musical transient shaper.

    A dual-envelope (fast / slow) RMS detector per channel produces a
    transient-versus-body balance in 0..1; attack and sustain gains are then
    cross-faded by that balance. This is the same split-band detector approach
    used by the ZPunch processor, built on the shared ZEnvelopeFollower.

    Zero added latency: the detector is causal, so the shaper never delays the
    signal it is shaping. */
class TransientShaperModule final : public DspModule
{
public:
    TransientShaperModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    static constexpr int kMaxChannels = 2;

    enum ParameterIndex
    {
        AttackDb = 0,
        SustainDb,
        AttackTime,
        ReleaseTime,
        Sensitivity,
        NumParameters
    };

private:
    void applyParameters() noexcept;

    std::array<ZEnvelopeFollower, kMaxChannels> m_fastEnvelope;
    std::array<ZEnvelopeFollower, kMaxChannels> m_slowEnvelope;

    float m_values[NumParameters] {};
    int m_appliedChannels = 0;
    float m_attackGain = 1.0f;
    float m_sustainGain = 1.0f;
    float m_emphasis = 1.0f;
    bool m_dirty = true;
};

} // namespace zfactory
