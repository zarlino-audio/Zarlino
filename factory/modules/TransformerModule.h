#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.saturation.transformer - iron/transformer colour.

    Why this is not "another saturation": a transformer saturates only where its
    core actually saturates - the LOW end - while its leakage inductance rolls off
    the top. So the band is split complementarily at `splitHz`, ONLY the low band
    is driven into the non-linearity, and the high band is passively rolled off.

    The result is the classic console/iron behaviour: thick low mids, a slightly
    soft top, and a midrange that is left alone. A full-band saturator cannot
    produce it, however it is tuned.

    The split is complementary (`low = lowpass(x)`, `high = x - low`), so with
    drive at 0 dB and HF loss wide open the module is a true pass-through.
 */
class TransformerModule final : public DspModule
{
public:
    TransformerModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        DriveDb = 0,
        SplitHz,
        HfHz,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;

    MultiChannelBiquad m_lowPass;
    float m_hfState[2] { 0.0f, 0.0f };

    SmoothedParameter m_drive;
    SmoothedParameter m_hfCoefficient;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_coefficientsDirty = true;
};

} // namespace zfactory
