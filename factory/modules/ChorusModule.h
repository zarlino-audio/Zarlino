#pragma once

#include "ModulationSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.modulation.chorus - two modulated delay taps per channel.

    Musical role: the "make it wider / thicker" request. Two voices per channel
    at a fixed quarter-cycle offset, with a per-channel phase offset so the
    right side is not a copy of the left.

    Depth in milliseconds (not a 0..1 "amount") because that is the number that
    decides whether it reads as a chorus or as a doubler. */
class ChorusModule final : public DspModule
{
public:
    ChorusModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        RateHz = 0,
        DepthMs,
        Spread,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;

    static constexpr int kVoices = 2;

    FractionalDelayLine m_lines[2];
    ModLfo m_lfo[2];

    SmoothedParameter m_rate;
    SmoothedParameter m_depthSamples;
    SmoothedParameter m_spread;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_targetsDirty = true;
};

} // namespace zfactory
