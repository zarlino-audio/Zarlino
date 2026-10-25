#include "TransformerModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation.transformer";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Transformer";
    manifest.description = "Iron colour: low-band core saturation with a passive high-frequency roll-off";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.1f;

    {
        ParameterManifest parameter;
        parameter.id = "driveDb";
        parameter.displayName = "Drive";
        parameter.unit = "dB";
        parameter.minValue = 0.0f;
        parameter.maxValue = 18.0f;
        parameter.defaultValue = 4.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "splitHz";
        parameter.displayName = "Core Split";
        parameter.unit = "Hz";
        parameter.minValue = 60.0f;
        parameter.maxValue = 600.0f;
        parameter.defaultValue = 220.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "hfHz";
        parameter.displayName = "HF Roll-off";
        parameter.unit = "Hz";
        parameter.minValue = 3000.0f;
        parameter.maxValue = 20000.0f;
        parameter.defaultValue = 14000.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Tone";
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
TransformerModule::TransformerModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TransformerModule::getManifest() const noexcept
{
    return manifest();
}

void TransformerModule::prepare (const PrepareContext& context)
{
    m_sampleRate = std::max (1.0f, static_cast<float> (context.sampleRate));
    m_lowPass.prepare (std::max (1, context.numChannels));

    m_coefficientsDirty = true;
    recomputeTargets();

    m_drive.prepare (context.sampleRate, 0.03, m_drive.target());
    m_hfCoefficient.prepare (context.sampleRate, 0.03, m_hfCoefficient.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void TransformerModule::recomputeTargets() noexcept
{
    m_drive.setTarget (std::pow (10.0f, m_values[DriveDb] / 20.0f));

    const float bounded = std::min (m_values[HfHz], m_sampleRate * 0.45f);
    m_hfCoefficient.setTarget (1.0f - std::exp (-2.0f * 3.14159265358979f * bounded / m_sampleRate));

    m_mix.setTarget (m_values[Mix]);

    if (m_coefficientsDirty)
    {
        const float corner = std::min (m_values[SplitHz], m_sampleRate * 0.4f);
        m_lowPass.setCoefficients (RBJ::makeLowPass (m_sampleRate, corner, 0.70710678f));
        m_coefficientsDirty = false;
    }
}

void TransformerModule::reset() noexcept
{
    recomputeTargets();

    m_lowPass.reset();
    m_hfState[0] = 0.0f;
    m_hfState[1] = 0.0f;

    m_drive.setImmediate (m_drive.target());
    m_hfCoefficient.setImmediate (m_hfCoefficient.target());
    m_mix.setImmediate (m_mix.target());
}

void TransformerModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;

    if (parameterIndex == SplitHz)
        m_coefficientsDirty = true;

    recomputeTargets();
}

float TransformerModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TransformerModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    for (int channel = 0; channel < numChannels && channel < 2; ++channel)
    {
        float* data = channels[channel];

        if (data == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = m_drive.next();
            const float hf = m_hfCoefficient.next();
            const float mix = m_mix.next();

            const float dry = data[i];

            // Complementary split: the low band is what the core sees, the rest
            // is what passes through it untouched.
            float low[1] { dry };
            float* lowChannel[1] { low };
            m_lowPass.process (lowChannel, 1, 1);

            const float high = dry - low[0];

            const float saturated = std::tanh (low[0] * drive) / std::max (1.0e-3f, std::tanh (drive));

            // Leakage inductance rolls the top off passively.
            m_hfState[channel] += hf * (high - m_hfState[channel]);

            const float wet = saturated + m_hfState[channel];
            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

} // namespace zfactory
