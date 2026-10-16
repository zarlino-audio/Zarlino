#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.filter.multimode - resonant low-pass / high-pass with a 12 or
    24 dB/oct slope.

    Built on MultiChannelBiquad + the RBJ cookbook coefficient generators from
    shared/dsp, cascaded into two sections for the 24 dB/oct setting. A zero-Q
    load (Q = 0.707) is maximally flat, so at default resonance the 24 dB/oct
    setting is a clean Butterworth-squared response. */
class FilterModule final : public DspModule
{
public:
    FilterModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        Shape = 0,      // 0 = low pass, 1 = high pass
        Frequency,
        Resonance,
        Slope,          // 0 = 12 dB/oct, 1 = 24 dB/oct
        NumParameters
    };

private:
    void updateCoefficients() noexcept;

    MultiChannelBiquad m_sectionA;
    MultiChannelBiquad m_sectionB;

    BiquadCoefficients m_pendingA;
    BiquadCoefficients m_pendingB;
    bool m_coefficientsDirty = true;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_highPass = false;
    bool m_twoSections = false;
};

} // namespace zfactory
