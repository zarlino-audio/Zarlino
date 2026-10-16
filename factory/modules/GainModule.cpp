#include "GainModule.h"

#include <algorithm>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.gain";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Gain";
    manifest.description = "Smoothed trim stage with polarity inversion";
    manifest.category = ModuleCategory::Utility;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.05f;

    {
        ParameterManifest parameter;
        parameter.id = "gainDb";
        parameter.displayName = "Gain";
        parameter.unit = "dB";
        parameter.minValue = -60.0f;
        parameter.maxValue = 24.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Trim";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "polarity";
        parameter.displayName = "Polarity";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 2;
        parameter.displayGroup = "Trim";
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
GainModule::GainModule()
{
    m_values[GainDb] = manifest().parameters[GainDb].defaultValue;
    m_values[Polarity] = manifest().parameters[Polarity].defaultValue;
}

const ModuleManifest& GainModule::getManifest() const noexcept
{
    return manifest();
}

void GainModule::prepare (const PrepareContext& context)
{
    m_gain.prepare (context.sampleRate, 0.02, m_values[GainDb]);
    m_polarity = m_values[Polarity] >= 0.5f ? -1.0f : 1.0f;
}

void GainModule::reset() noexcept
{
    m_gain.reset (m_values[GainDb]);
}

void GainModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);
    m_values[parameterIndex] = clamped;

    if (parameterIndex == GainDb)
    {
        m_gain.setTarget (clamped);
    }
    else
    {
        m_polarity = clamped >= 0.5f ? -1.0f : 1.0f;
    }
}

float GainModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void GainModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    const float target = m_values[GainDb];
    m_gain.setTarget (target);

    const float staticGain = zdsp::decibelsToGain (target, -100.0f);

    // Fast path: when the ramp has already settled, a single multiply per sample.
    if (m_gain.current() == target)
    {
        const float gain = staticGain * m_polarity;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channels[ch];

            if (data == nullptr)
                continue;

            for (int i = 0; i < numSamples; ++i)
                data[i] *= gain;
        }

        m_gain.skip (numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = zdsp::decibelsToGain (m_gain.next(), -100.0f) * m_polarity;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channels[ch];

            if (data != nullptr)
                data[i] *= gain;
        }
    }
}

} // namespace zfactory
