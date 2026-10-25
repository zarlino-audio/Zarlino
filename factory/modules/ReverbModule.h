#pragma once

#include "ModuleSupport.h"

#include <vector>

namespace zfactory
{

//==============================================================================
/** zarlino.reverb.algorithmic - a Freeverb-style room: eight damped comb
    resonators into four allpass diffusers, per channel, with a stereo spread.

    Musical role: short ambience through to a long hall, a dark tail, and the
    "spatial" half of any request that says reverb, room, plate, hall, space or
    ambience. Damping is exposed because a dark reverb is a different sound from
    a bright one, not a different algorithm.

    Predelay is a real delay line in front of the network, so a request for
    "predelay" is answered by the time the tail starts rather than by a
    compromise.

    Determinism: every tuning is a fixed integer derived from the sample rate,
    the damping filters are one-pole with stored state, and reset() clears every
    line - so repeated prepare/reset cycles render bit-identically. */
class ReverbModule final : public DspModule
{
public:
    ReverbModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        PredelayMs = 0,
        Size,
        Decay,
        Damping,
        Width,
        Mix,
        NumParameters
    };

private:
    struct Comb
    {
        std::vector<float> buffer;
        int index = 0;
        float store = 0.0f;

        void resize (int length) { buffer.assign (static_cast<size_t> (std::max (1, length)), 0.0f); index = 0; store = 0.0f; }
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); index = 0; store = 0.0f; }

        float process (float input, float feedback, float damping) noexcept
        {
            const float output = buffer[static_cast<size_t> (index)];
            store = output * (1.0f - damping) + store * damping;
            buffer[static_cast<size_t> (index)] = flushDenormal (input + store * feedback);

            if (++index >= static_cast<int> (buffer.size()))
                index = 0;

            return output;
        }
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int index = 0;

        void resize (int length) { buffer.assign (static_cast<size_t> (std::max (1, length)), 0.0f); index = 0; }
        void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); index = 0; }

        float process (float input) noexcept
        {
            const float stored = buffer[static_cast<size_t> (index)];
            const float output = -input + stored;
            buffer[static_cast<size_t> (index)] = flushDenormal (input + stored * 0.5f);

            if (++index >= static_cast<int> (buffer.size()))
                index = 0;

            return output;
        }
    };

    void recomputeTargets() noexcept;

    static constexpr int kCombsPerChannel = 8;
    static constexpr int kAllpassesPerChannel = 4;

    Comb m_combs[2][kCombsPerChannel];
    Allpass m_allpasses[2][kAllpassesPerChannel];

    std::vector<float> m_predelay[2];
    int m_predelayLength = 0;
    int m_predelayIndex = 0;

    SmoothedParameter m_feedback;
    SmoothedParameter m_damping;
    SmoothedParameter m_mix;
    SmoothedParameter m_width;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_targetsDirty = true;
};

} // namespace zfactory
