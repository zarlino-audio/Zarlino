#pragma once

#include "ModuleSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.saturation.clipper - a driven clipper with a ceiling.

    Musical role: "make it louder and more aggressive" without the colour of a
    tape or tube emulation. Three shapes that are genuinely different transfer
    curves:

      soft        tanh  - odd harmonics only, the gentle choice
      hard        clamp - the digital/hardcore choice, dense odd harmonics
      asymmetric  tanh on the positive half, a softer tanh on the negative half,
                  which is what produces the EVEN harmonics a symmetric clip
                  cannot

    The ceiling is applied after the non-linearity, so the output is bounded by
    the ceiling by construction - a clipper that could exceed its own ceiling
    would not be a clipper. */
class ClipperModule final : public DspModule
{
public:
    ClipperModule();

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
        CeilingDb,
        Mode,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;
    float shape (float value, int mode) const noexcept;

    SmoothedParameter m_driveGain;
    SmoothedParameter m_ceilingGain;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    bool m_targetsDirty = true;
};

} // namespace zfactory
