#pragma once

#include "ModuleSupport.h"

#include "ZEqualizer.h"

namespace zfactory
{

//==============================================================================
/** zarlino.eq.parametric - four-band parametric equaliser.

    Wraps the shared ZEqualizer (serial RBJ biquad cascade, zero allocation in
    process) with a fixed, conventional band layout:

        band 0 - low shelf      (20 Hz .. 1 kHz)
        band 1 - peak           (60 Hz .. 8 kHz)
        band 2 - peak           (300 Hz .. 16 kHz)
        band 3 - high shelf     (2 kHz .. 20 kHz)

    Every band exposes frequency, gain and Q. */
class ParametricEqModule final : public DspModule
{
public:
    ParametricEqModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    static constexpr int kNumBands = 4;

    enum ParameterIndex
    {
        LowShelfFrequency = 0,
        LowShelfGain,
        LowShelfQ,
        PeakLowFrequency,
        PeakLowGain,
        PeakLowQ,
        PeakHighFrequency,
        PeakHighGain,
        PeakHighQ,
        HighShelfFrequency,
        HighShelfGain,
        HighShelfQ,
        NumParameters
    };

private:
    void pushBand (int band) noexcept;

    ZEqualizer<kNumBands> m_equalizer;
    float m_values[NumParameters] {};
    bool m_dirty[kNumBands] {};
};

} // namespace zfactory
