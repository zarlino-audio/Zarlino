#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.dynamics.deesser - band-limited downward compression.

    Musical role: the de-esser, and the same mechanism answers a "tame the
    harshness" request on cymbals, guitars and distorted vocals.

    The band split is COMPLEMENTARY (`high = input - lowpass(input)`) rather than
    a low-pass/high-pass pair. That makes `low + high` exactly the input, so with
    no gain reduction the module is a true pass-through - a crossover pair would
    leave a phase notch at the crossover frequency even when idle.

    Only the high band is attenuated, and the reduction is derived from the high
    band's own envelope, which is what keeps the body of the signal intact. */
class DeEsserModule final : public DspModule
{
public:
    DeEsserModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        FreqHz = 0,
        ThresholdDb,
        Amount,
        ReleaseMs,
        NumParameters
    };

private:
    void updateCoefficients() noexcept;
    void recomputeRelease() noexcept;

    MultiChannelBiquad m_lowPass;

    float m_envelope[2] { 0.0f, 0.0f };
    float m_gain[2] { 1.0f, 1.0f };

    SmoothedParameter m_amount;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    float m_releaseCoefficient = 0.0f;
    bool m_coefficientsDirty = true;
    bool m_releaseDirty = true;
};

} // namespace zfactory
