#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.saturation.tape - tape-machine character.

    Three physical behaviours, in the order they happen in a machine:

      1. HEAD BUMP   a low-frequency shelf lift around 80 Hz, the resonance of
                     the reproduce head. It is what makes tape sound "big"
                     rather than merely dull.
      2. ASYMMETRY    a tape machine is biased, so its transfer curve is not
                     symmetric: the positive and negative halves compress
                     differently and the result carries EVEN harmonics. Modelled
                     by shifting the operating point (`tanh(x * drive + bias)`)
                     and subtracting `tanh(bias)` so the DC the bias would add is
                     never emitted.
      3. HF LOSS     a one-pole roll-off, because a finite gap cannot reproduce
                     arbitrarily short wavelengths.

    Automatic make-up gain keeps full scale near unity, so the drive control
    changes character rather than level. */
class TapeModule final : public DspModule
{
public:
    TapeModule();

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
        Bias,
        BumpDb,
        HfHz,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;

    MultiChannelBiquad m_headBump;
    float m_hfState[2] { 0.0f, 0.0f };

    SmoothedParameter m_drive;
    SmoothedParameter m_bias;
    SmoothedParameter m_hfCoefficient;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_coefficientsDirty = true;
};

} // namespace zfactory
