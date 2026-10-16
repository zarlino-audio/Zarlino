#pragma once

#include "ModuleSupport.h"

#include "ZDynamics.h"

#include <array>

namespace zfactory
{

//==============================================================================
/** zarlino.dynamics.gate - downward expander with hysteresis.

    Wraps ZGate (shared/dsp), one instance per channel. Threshold, range, attack
    (opening speed), release (closing speed) and hysteresis are exposed. */
class GateModule final : public DspModule
{
public:
    GateModule();

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
        Threshold = 0,
        Range,
        Attack,
        Release,
        Hysteresis,
        NumParameters
    };

private:
    void applyParameters() noexcept;

    std::array<ZGate, kMaxChannels> m_gates;
    float m_values[NumParameters] {};
    int m_appliedChannels = 0;
    bool m_dirty = true;
};

} // namespace zfactory
