#pragma once

#include "ModuleSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.stereo.width - mid/side width control with an independent bass-mono
    guard (the same equal-width law used by the PresenceField processor).

    The mono configuration is legal and is an exact pass-through. */
class StereoWidthModule final : public DspModule
{
public:
    StereoWidthModule();

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
        BassMonoAmount,
        BassMonoFreq,
        NumParameters
    };

private:
    void updateBassMonoCoefficient() noexcept;

    SmoothedParameter m_width;
    float m_values[NumParameters] {};
    float m_bassMonoCoeff = 0.0f;
    float m_bassMonoAmount = 0.0f;
    float m_sideLow = 0.0f;
    float m_appliedBassMonoFreq = 0.0f;
    double m_sampleRate = 48000.0;
};

} // namespace zfactory
