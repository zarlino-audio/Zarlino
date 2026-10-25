#pragma once

#include "ModulationSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.modulation.phaser - a chain of first-order allpass stages whose
    break frequency is swept by an LFO.

    Musical role: the swirl. Unlike the flanger it moves *phase* rather than
    delay, so the notches are not harmonically spaced - which is why it sounds
    liquid rather than metallic.

    Feedback is taken from the last stage back into the first (the classic
    "resonant phaser"); it is clamped below 0.95 so the loop always decays. */
class PhaserModule final : public DspModule
{
public:
    PhaserModule();

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
        CentreHz,
        Depth,
        Feedback,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;
    float allpassCoefficient (float frequencyHz) const noexcept;

    static constexpr int kStages = 6;

    float m_state[2][kStages] {};
    float m_feedbackState[2] { 0.0f, 0.0f };
    ModLfo m_lfo[2];

    SmoothedParameter m_rate;
    SmoothedParameter m_depth;
    SmoothedParameter m_feedback;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_targetsDirty = true;
};

} // namespace zfactory
