#pragma once

#include "ModuleSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.gain - trimmed gain stage with an independent polarity switch.
    Off-the-shelf Zarlino utility: the same smoothed dB mapping used by every
    plugin's input/output trim. */
class GainModule final : public DspModule
{
public:
    GainModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        GainDb = 0,
        Polarity,
        NumParameters
    };

private:
    SmoothedParameter m_gain;
    float m_values[NumParameters] {};
    float m_polarity = 1.0f;
};

} // namespace zfactory
