#pragma once

#include "ModulationSupport.h"

namespace zfactory
{

//==============================================================================
/** zarlino.modulation.flanger - one very short modulated delay with feedback.

    Musical role: the jet-plane sweep. The distinction from the chorus is
    entirely physical - delay under about 10 ms with feedback comb-filters the
    signal, which is the flange; longer without feedback is the chorus.

    Feedback is bipolar so a request for a "hollow" or "inverted" flange is
    expressible; it is clamped below 1.0 (|fb| <= 0.9) because at unity the loop
    never decays. */
class FlangerModule final : public DspModule
{
public:
    FlangerModule();

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
        Feedback,
        Mix,
        NumParameters
    };

private:
    void recomputeTargets() noexcept;

    FractionalDelayLine m_lines[2];
    ModLfo m_lfo[2];
    float m_feedbackState[2] { 0.0f, 0.0f };

    SmoothedParameter m_rate;
    SmoothedParameter m_depthSamples;
    SmoothedParameter m_feedback;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_targetsDirty = true;
};

} // namespace zfactory
