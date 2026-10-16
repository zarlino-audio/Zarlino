#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.eq.tilt - tilt equaliser: a matched low-shelf / high-shelf pair that
    rotates the spectrum around a pivot frequency by -tilt/2 and +tilt/2 dB.

    0 dB is an exact pass-through: the RBJ shelf generators collapse to unity
    coefficients when the gain factor is 1, so a tilt of zero leaves the signal
    bit-identical. */
class TiltEqModule final : public DspModule
{
public:
    TiltEqModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        TiltDb = 0,
        PivotHz,
        NumParameters
    };

private:
    void updateCoefficients() noexcept;

    MultiChannelBiquad m_lowShelf;
    MultiChannelBiquad m_highShelf;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_coefficientsDirty = true;
};

} // namespace zfactory
