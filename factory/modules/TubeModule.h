#pragma once

#include "ModuleSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.saturation.tube - a driven tube stage with supply sag.

    What separates this from a plain asymmetric clipper is the SAG control: a
    real single-ended stage cannot deliver unlimited current, so a transient pulls
    the operating point down for a few tens of milliseconds and the gain recovers
    slowly afterwards. That envelope-dependent gain is the "breath" players hear;
    it is modelled here with a slow follower on the post-drive signal.

    Asymmetry uses the same bias trick as the tape module - shift the operating
    point, then subtract the resulting DC - so even harmonics are generated
    without a constant offset.
 */
class TubeModule final : public DspModule
{
public:
    TubeModule();

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
        Sag,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;

    SmoothedParameter m_drive;
    SmoothedParameter m_bias;
    SmoothedParameter m_sag;
    SmoothedParameter m_mix;

    float m_sagState = 0.0f;
    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
};

} // namespace zfactory
