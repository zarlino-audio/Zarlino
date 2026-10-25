#pragma once

#include "ModulationSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.modulation.tremolo - amplitude modulation with a stereo phase
    offset, so the same module is a tremolo (both sides together) or an auto-pan
    (sides opposed) depending on one control.

    Musical role: movement without pitch artefacts. Unlike the delay-based
    modulators this one cannot smear a transient, which is why it is the right
    answer to "make it pulse" on drums.

    Shape is a stepped 0/1/2 (sine / triangle / square): the three that sound
    different, rather than a continuous morph nobody sets to 0.37.

    Both channels read the SAME phase accumulator, the right one offset by
    `stereoPhase` cycles. That makes the offset exact (half a cycle is a perfect
    auto-pan) and it cannot drift, which two free-running oscillators would. */
class TremoloModule final : public DspModule
{
public:
    TremoloModule();

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
        Depth,
        Shape,
        StereoPhase,
        NumParameters
    };

private:
    SmoothedParameter m_rate;
    SmoothedParameter m_depth;
    SmoothedParameter m_phaseOffset;

    float m_values[NumParameters] {};
    float m_phase = 0.0f;
    float m_sampleRate = 48000.0f;
};

} // namespace zfactory
