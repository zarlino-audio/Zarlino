#pragma once

#include "ModuleSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.mix.drywet - parallel dry/wet blend with an optional equal-power law.

    The module keeps its own copy of the pre-module signal, so it can be placed
    anywhere in a chain (not just at the end). Buffers are allocated in
    prepare() and never on the audio thread. */
class DryWetModule final : public DspModule
{
public:
    DryWetModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        Mix = 0,
        EqualPower,
        NumParameters
    };

private:
    SmoothedParameter m_mix;
    std::vector<float> m_dryBuffer;   // numChannels * maxBlockSize
    int m_channelCount = 0;
    int m_maxBlockSize = 0;
    float m_values[NumParameters] {};
    bool m_equalPower = false;
};

} // namespace zfactory
