#include "ClipperModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation.clipper";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Clipper";
    manifest.description = "Driven soft/hard/asymmetric clipper with a hard ceiling";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.5f;

    {
        ParameterManifest parameter;
        parameter.id = "driveDb";
        parameter.displayName = "Drive";
        parameter.unit = "dB";
        parameter.minValue = 0.0f;
        parameter.maxValue = 24.0f;
        parameter.defaultValue = 6.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "ceilingDb";
        parameter.displayName = "Ceiling";
        parameter.unit = "dB";
        parameter.minValue = -12.0f;
        parameter.maxValue = 0.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Output";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "mode";
        parameter.displayName = "Mode";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 2.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 3;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "mix";
        parameter.displayName = "Mix";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 1.0f;
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
ClipperModule::ClipperModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& ClipperModule::getManifest() const noexcept
{
    return manifest();
}

void ClipperModule::prepare (const PrepareContext& context)
{
    recomputeTargets();

    m_driveGain.prepare (context.sampleRate, 0.03, m_driveGain.target());
    m_ceilingGain.prepare (context.sampleRate, 0.03, m_ceilingGain.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void ClipperModule::recomputeTargets() noexcept
{
    m_driveGain.setTarget (std::pow (10.0f, m_values[DriveDb] / 20.0f));
    m_ceilingGain.setTarget (std::pow (10.0f, m_values[CeilingDb] / 20.0f));
    m_mix.setTarget (m_values[Mix]);
    m_targetsDirty = false;
}

void ClipperModule::reset() noexcept
{
    recomputeTargets();

    m_driveGain.setImmediate (m_driveGain.target());
    m_ceilingGain.setImmediate (m_ceilingGain.target());
    m_mix.setImmediate (m_mix.target());
}

void ClipperModule::setParameter (int parameterIndex, float value) noexcept
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

float ClipperModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

float ClipperModule::shape (float value, int mode) const noexcept
{
    switch (mode)
    {
        case 1: // hard
            return std::min (1.0f, std::max (-1.0f, value));

        case 2: // asymmetric: the negative half compresses earlier, adding evens
            return value >= 0.0f ? std::tanh (value) : std::tanh (value * 0.7f) * 0.9f;

        default: // soft
            return std::tanh (value);
    }
}

void ClipperModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_targetsDirty)
        recomputeTargets();

    const int mode = static_cast<int> (std::lround (m_values[Mode]));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* data = channels[channel];

        if (data == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = m_driveGain.next();
            const float ceiling = m_ceilingGain.next();
            const float mix = m_mix.next();

            const float dry = data[i];
            const float wet = shape (dry * drive, mode) * ceiling;

            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

} // namespace zfactory
