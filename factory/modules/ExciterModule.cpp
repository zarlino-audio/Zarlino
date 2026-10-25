#include "ExciterModule.h"

#include "ZBiquadFilter.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation.exciter";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Exciter";
    manifest.description = "High-band harmonic generator for air and presence";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.1f;

    {
        ParameterManifest parameter;
        parameter.id = "freqHz";
        parameter.displayName = "Tune";
        parameter.unit = "Hz";
        parameter.minValue = 800.0f;
        parameter.maxValue = 8000.0f;
        parameter.defaultValue = 3000.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "amount";
        parameter.displayName = "Amount";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.35f;
        parameter.smoothingMs = 30.0f;
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
ExciterModule::ExciterModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& ExciterModule::getManifest() const noexcept
{
    return manifest();
}

void ExciterModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);

    const int channels = std::max (1, context.numChannels);
    m_bandHighPass.prepare (channels);
    m_dcHighPass.prepare (channels);
    m_coefficientsDirty = true;
    updateCoefficients();

    m_amount.prepare (context.sampleRate, 0.03, m_values[Amount]);
    m_mix.prepare (context.sampleRate, 0.03, m_values[Mix]);

    reset();
}

void ExciterModule::updateCoefficients() noexcept
{
    const float nyquist = m_sampleRate * 0.5f;
    const float band = std::min (m_values[FreqHz], nyquist * 0.5f);

    m_bandHighPass.setCoefficients (RBJ::makeHighPass (m_sampleRate, band, 0.70710678f));
    // The rectifier's DC and the residual fundamental have to go, or the module
    // would add a constant offset to every sample.
    m_dcHighPass.setCoefficients (RBJ::makeHighPass (m_sampleRate, band, 0.70710678f));
    m_coefficientsDirty = false;
}

void ExciterModule::reset() noexcept
{
    m_bandHighPass.reset();
    m_dcHighPass.reset();
    m_amount.setImmediate (m_values[Amount]);
    m_mix.setImmediate (m_values[Mix]);
}

void ExciterModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;

    switch (parameterIndex)
    {
        case Amount: m_amount.setTarget (clamped); break;
        case Mix:    m_mix.setTarget (clamped); break;
        default:     m_coefficientsDirty = true; break;
    }
}

float ExciterModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void ExciterModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_coefficientsDirty)
        updateCoefficients();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* data = channels[channel];

        if (data == nullptr)
            continue;

        // Band-limit a copy, rectify it, remove the DC, then blend. One sample
        // at a time through the biquads keeps the state exact and avoids a
        // scratch buffer entirely.
        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];

            float band[1] { dry };
            float* bandChannel[1] { band };
            m_bandHighPass.process (bandChannel, 1, 1);

            float rectified[1] { std::abs (band[0]) };
            float* rectifiedChannel[1] { rectified };
            m_dcHighPass.process (rectifiedChannel, 1, 1);

            const float amount = m_amount.next();
            const float mix = m_mix.next();

            const float wet = dry + rectified[0] * amount;
            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

} // namespace zfactory
