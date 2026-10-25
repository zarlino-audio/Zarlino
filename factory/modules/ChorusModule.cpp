#include "ChorusModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
constexpr float kMaxDepthMs = 20.0f;
constexpr float kBaseDelayMs = 8.0f;

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.modulation.chorus";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Chorus";
    manifest.description = "Two-voice modulated delay for width and thickness";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.6f;

    {
        ParameterManifest parameter;
        parameter.id = "rateHz";
        parameter.displayName = "Rate";
        parameter.unit = "Hz";
        parameter.minValue = 0.05f;
        parameter.maxValue = 8.0f;
        parameter.defaultValue = 0.6f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 50.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "depthMs";
        parameter.displayName = "Depth";
        parameter.unit = "ms";
        parameter.minValue = 0.0f;
        parameter.maxValue = kMaxDepthMs;
        parameter.defaultValue = 6.0f;
        parameter.smoothingMs = 50.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "spread";
        parameter.displayName = "Spread";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.6f;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "mix";
        parameter.displayName = "Mix";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.35f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Output";
        manifest.parameters.push_back (parameter);
    }

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
ChorusModule::ChorusModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& ChorusModule::getManifest() const noexcept
{
    return manifest();
}

void ChorusModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);

    const int length = static_cast<int> (std::ceil ((kBaseDelayMs + kMaxDepthMs + 2.0f) * 0.001f * m_sampleRate)) + 4;

    for (int channel = 0; channel < 2; ++channel)
    {
        m_lines[channel].resize (length);
        m_lfo[channel].prepare (context.sampleRate);
    }

    recomputeTargets();

    m_rate.prepare (context.sampleRate, 0.05, m_rate.target());
    m_depthSamples.prepare (context.sampleRate, 0.05, m_depthSamples.target());
    m_spread.prepare (context.sampleRate, 0.04, m_spread.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void ChorusModule::recomputeTargets() noexcept
{
    m_rate.setTarget (m_values[RateHz]);
    m_depthSamples.setTarget (m_values[DepthMs] * 0.001f * m_sampleRate);
    m_spread.setTarget (m_values[Spread]);
    m_mix.setTarget (m_values[Mix]);
    m_targetsDirty = false;
}

void ChorusModule::reset() noexcept
{
    recomputeTargets();

    for (int channel = 0; channel < 2; ++channel)
    {
        m_lines[channel].clear();
        m_lfo[channel].reset (channel == 0 ? 0.0f : 0.25f);
    }

    m_rate.setImmediate (m_rate.target());
    m_depthSamples.setImmediate (m_depthSamples.target());
    m_spread.setImmediate (m_spread.target());
    m_mix.setImmediate (m_mix.target());
}

void ChorusModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;
    recomputeTargets();
}

float ChorusModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void ChorusModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_lines[0].length() <= 1)
        return;

    if (m_targetsDirty)
        recomputeTargets();

    const bool stereo = numChannels > 1;
    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    const float baseSamples = kBaseDelayMs * 0.001f * m_sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        const float rate = m_rate.next();
        const float depth = m_depthSamples.next();
        const float spread = m_spread.next();
        const float mix = m_mix.next();

        m_lfo[0].setRate (rate);
        m_lfo[1].setRate (rate);

        const float dryLeft = left[i];
        const float dryRight = right[i];

        // Voice A and B sit a quarter cycle apart; the right channel is offset
        // again by up to half a cycle, which is what widens the image.
        const float phaseLeft = m_lfo[0].next (0) * 0.5f + 0.5f;
        const float phaseRight = m_lfo[1].next (0) * 0.5f + 0.5f;

        const float leftOffset = depth * (0.5f + 0.5f * phaseLeft);
        const float rightOffset = depth * (0.5f + 0.5f * phaseRight) * (1.0f - 0.5f * spread);

        const float wetLeft = m_lines[0].read (baseSamples + leftOffset);
        const float wetRight = m_lines[1].read (baseSamples + rightOffset);

        m_lines[0].write (dryLeft);
        m_lines[1].write (dryRight);

        left[i] = dryLeft * (1.0f - mix) + wetLeft * mix;
        right[i] = dryRight * (1.0f - mix) + wetRight * mix;
    }
}

} // namespace zfactory
