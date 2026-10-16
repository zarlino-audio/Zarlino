#pragma once

#include "ModuleSupport.h"

#include "Saturator.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace zfactory
{

//==============================================================================
/** zarlino.saturation - six-mode harmonic saturator.

    This module is a thin adapter over the shipped ZScorch `Saturator`: the same
    voiced, C1-continuous transfer curves and the same ADAA-1 anti-aliasing that
    ZScorch uses in production, so the factory adds no second saturation engine
    and cannot drift from the certified product sound.

    The default ADAA path is zero latency and a fraction of the cost of 4x
    oversampling, which is why the factory uses it. */
class SaturationModule final : public DspModule
{
public:
    SaturationModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override;

    enum ParameterIndex
    {
        Drive = 0,
        Mode,
        HarmReduction,
        NumParameters
    };

    static constexpr int kNumModes = 6;

private:
    static SaturationMode toMode (float value) noexcept;

    Saturator m_saturator;
    juce::AudioBuffer<float> m_view;
    float m_values[NumParameters] {};
    int m_preparedChannels = 0;
};

} // namespace zfactory
