#pragma once

#include "ModuleSupport.h"

#include <vector>

namespace zfactory
{

//==============================================================================
/** zarlino.delay.stereo - a stereo delay with a damped feedback path.

    Musical role: slapback, rhythmic echo, ping-pong. The delay time is
    smoothed and read with linear interpolation, so automating it sweeps the
    pitch of the tail (what a tape delay does) instead of clicking.

    Feedback is low-passed inside the loop, which is what keeps a long repeat
    tail from turning into a bright pile-up.

    Ping-pong is a stepped 0/1 switch rather than a continuous blend: half-way
    between the two routings is not a sound anybody wants, and a stepped value
    is trivially determined (no interpolation ambiguity in a saved state). */
class DelayModule final : public DspModule
{
public:
    DelayModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        TimeMs = 0,
        Feedback,
        DampHz,
        PingPong,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;
    float read (int channel, float delaySamples) const noexcept;

    std::vector<float> m_lines[2];
    int m_length = 0;
    int m_writeIndex = 0;

    float m_damping[2] { 0.0f, 0.0f };

    SmoothedParameter m_delaySamples;
    SmoothedParameter m_feedback;
    SmoothedParameter m_mix;
    SmoothedParameter m_dampCoefficient;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_targetsDirty = true;
};

} // namespace zfactory
