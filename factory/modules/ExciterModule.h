#pragma once

#include "ModuleSupport.h"

#include "ZBiquadFilter.h"

namespace zfactory
{

//==============================================================================
/** zarlino.saturation.exciter - high-band harmonic generation ("air"/presence).

    Musical role: the request behind "open it up", "more air", "make it cut".
    A band above `freqHz` is rectified (which generates harmonics the original
    band did not contain), DC and the original partials are removed by a second
    high-pass, and the result is blended back.

    This is the analogue-style approach on purpose: no aliasing-suppressed
    oversampled shaper, because an exciter is by definition adding energy at and
    above the source band, and a rectifier's products are harmonics of the band
    rather than of the sample rate.

    `amount` is a linear blend into the added signal (not a drive control), so
    the module is bounded: the output cannot exceed the input plus the full
    rectified band. */
class ExciterModule final : public DspModule
{
public:
    ExciterModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    enum ParameterIndex
    {
        FreqHz = 0,
        Amount,
        Mix,
        NumParameters
    };

private:
    void updateCoefficients() noexcept;

    MultiChannelBiquad m_bandHighPass;
    MultiChannelBiquad m_dcHighPass;

    SmoothedParameter m_amount;
    SmoothedParameter m_mix;

    float m_values[NumParameters] {};
    float m_sampleRate = 48000.0f;
    bool m_coefficientsDirty = true;
};

} // namespace zfactory
