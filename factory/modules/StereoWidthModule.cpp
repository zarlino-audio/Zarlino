#include "StereoWidthModule.h"

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.stereo.width";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Stereo Width";
    manifest.description = "Mid/side width control with bass-mono guard";
    manifest.category = ModuleCategory::Stereo;
    manifest.certification = CertificationState::Certified;

    // Stereo only, by design: a width control has no meaning in a mono graph, so
    // the registry must refuse to build it there rather than silently no-op.
    manifest.supportedChannels = { ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.3f;

    {
        ParameterManifest parameter;
        parameter.id = "width";
        parameter.displayName = "Width";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 2.0f;
        parameter.defaultValue = 1.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Image";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "bassMonoAmount";
        parameter.displayName = "Bass Mono";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Image";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "bassMonoFreq";
        parameter.displayName = "Bass Mono Freq";
        parameter.unit = "Hz";
        parameter.minValue = 20.0f;
        parameter.maxValue = 500.0f;
        parameter.defaultValue = 120.0f;
        parameter.logarithmic = true;
        parameter.displayGroup = "Image";
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
StereoWidthModule::StereoWidthModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& StereoWidthModule::getManifest() const noexcept
{
    return manifest();
}

void StereoWidthModule::prepare (const PrepareContext& context)
{
    m_sampleRate = context.sampleRate;
    m_width.prepare (context.sampleRate, 0.03, m_values[Width]);
    m_bassMonoAmount = m_values[BassMonoAmount];
    m_sideLow = 0.0f;
    m_appliedBassMonoFreq = 0.0f;
    updateBassMonoCoefficient();
}

void StereoWidthModule::updateBassMonoCoefficient() noexcept
{
    // Keep the corner well below Nyquist so the one-pole stays stable at every
    // supported sample rate.
    const float frequency = std::min (m_values[BassMonoFreq],
                                      static_cast<float> (m_sampleRate) * 0.45f);

    m_bassMonoCoeff = 1.0f - std::exp (-2.0f * 3.14159265358979f * frequency
                                       / static_cast<float> (m_sampleRate));
    m_appliedBassMonoFreq = m_values[BassMonoFreq];
}

void StereoWidthModule::reset() noexcept
{
    m_width.reset (m_values[Width]);
    m_sideLow = 0.0f;
}

void StereoWidthModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);
    m_values[parameterIndex] = clamped;

    if (parameterIndex == Width)
        m_width.setTarget (clamped);
    else if (parameterIndex == BassMonoAmount)
        m_bassMonoAmount = clamped;
}

float StereoWidthModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void StereoWidthModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    // Defensive guard: the registry only builds this module for stereo graphs,
    // but a directly constructed instance must not read a channel it was not given.
    if (numChannels < 2 || channels[0] == nullptr || channels[1] == nullptr)
        return;

    if (m_values[BassMonoFreq] != m_appliedBassMonoFreq)
        updateBassMonoCoefficient();

    float* left = channels[0];
    float* right = channels[1];

    const float amount = std::min (1.0f, std::max (0.0f, m_bassMonoAmount));
    const float coefficient = m_bassMonoCoeff;

    for (int i = 0; i < numSamples; ++i)
    {
        const float width = m_width.next();

        const float mid = 0.5f * (left[i] + right[i]);
        float side = 0.5f * (left[i] - right[i]);

        m_sideLow += coefficient * (side - m_sideLow);
        m_sideLow = flushDenormal (m_sideLow);

        side = flushDenormal ((side - amount * m_sideLow) * width);

        left[i] = flushDenormal (mid + side);
        right[i] = flushDenormal (mid - side);
    }
}

} // namespace zfactory
