#pragma once

#include "ModuleSupport.h"

#include "ZDynamics.h"

#include <array>

namespace zfactory
{

//==============================================================================
/** zarlino.dynamics.compressor - feedforward compressor with a soft knee.

    Wraps ZCompressor (shared/dsp) with one instance per channel. Detection can
    be RMS or peak; threshold, ratio, attack, release, knee and makeup are all
    exposed. Zero latency: the compressor never looks ahead. */
class CompressorModule final : public DspModule
{
public:
    CompressorModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    static constexpr int kMaxChannels = 2;

    enum ParameterIndex
    {
        Threshold = 0,
        Ratio,
        Attack,
        Release,
        Knee,
        Makeup,
        Detection,   // 0 = RMS, 1 = peak
        NumParameters
    };

private:
    void applyParameters() noexcept;

    std::array<ZCompressor, kMaxChannels> m_compressors;
    float m_values[NumParameters] {};
    int m_appliedChannels = 0;
    bool m_dirty = true;
    bool m_peakMode = false;
};

} // namespace zfactory
