#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.stereo.midside - width control that keeps the bass mono.

    Musical role: "make it wider" and "keep the low end tight" are the same
    control surface in practice, and both are asked for constantly.

    Mid/side is computed exactly (no crossover summing), and only the SIDE
    signal is high-passed before it is scaled. That is what keeps a widened
    master from throwing its kick out of phase: below `bassMonoHz` the side gain
    falls away, so the bass stays centred while everything above widens.

    With width 1.0 and bassMonoHz at its minimum the module is not bit-exact
    pass-through (the side is still filtered), so the default is 1.0 with a
    20 Hz corner - inaudible, and honest about what the control does. */
class MidSideModule final : public DspModule
{
public:
    MidSideModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        Width = 0,
        BassMonoHz,
        NumParameters
    };

private:
    void updateCoefficients() noexcept;

    MultiChannelBiquad m_sideHighPass;

    SmoothedParameter m_width;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_coefficientsDirty = true;
};

} // namespace zfactory
