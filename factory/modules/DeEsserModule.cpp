#include "DeEsserModule.h"

#include "ZBiquadFilter.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.dynamics.deesser";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "De-Esser";
    manifest.description = "Band-limited downward compression for sibilance and harshness";
    manifest.category = ModuleCategory::Dynamics;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.3f;

    {
        ParameterManifest parameter;
        parameter.id = "freqHz";
        parameter.displayName = "Frequency";
        parameter.unit = "Hz";
        parameter.minValue = 2500.0f;
        parameter.maxValue = 12000.0f;
        parameter.defaultValue = 6000.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Detector";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "thresholdDb";
        parameter.displayName = "Threshold";
        parameter.unit = "dB";
        parameter.minValue = -48.0f;
        parameter.maxValue = 0.0f;
        parameter.defaultValue = -24.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Detector";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "amount";
        parameter.displayName = "Amount";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.6f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Reduction";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "releaseMs";
        parameter.displayName = "Release";
        parameter.unit = "ms";
        parameter.minValue = 20.0f;
        parameter.maxValue = 400.0f;
        parameter.defaultValue = 90.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Reduction";
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
DeEsserModule::DeEsserModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& DeEsserModule::getManifest() const noexcept
{
    return manifest();
}

void DeEsserModule::prepare (const PrepareContext& context)
{
    m_sampleRate = std::max (1.0f, static_cast<float> (context.sampleRate));
    m_lowPass.prepare (std::max (1, context.numChannels));

    m_coefficientsDirty = true;
    m_releaseDirty = true;
    updateCoefficients();
    recomputeRelease();

    m_amount.prepare (context.sampleRate, 0.03, m_values[Amount]);

    reset();
}

void DeEsserModule::updateCoefficients() noexcept
{
    const float nyquist = m_sampleRate * 0.5f;
    const float corner = std::min (m_values[FreqHz], nyquist * 0.5f);

    m_lowPass.setCoefficients (RBJ::makeLowPass (m_sampleRate, corner, 0.70710678f));
    m_coefficientsDirty = false;
}

void DeEsserModule::recomputeRelease() noexcept
{
    const float releaseSeconds = std::max (0.001f, m_values[ReleaseMs] * 0.001f);
    m_releaseCoefficient = std::exp (-1.0f / (releaseSeconds * m_sampleRate));
    m_releaseDirty = false;
}

void DeEsserModule::reset() noexcept
{
    m_lowPass.reset();
    m_envelope[0] = 0.0f;
    m_envelope[1] = 0.0f;
    m_gain[0] = 1.0f;
    m_gain[1] = 1.0f;
    m_amount.setImmediate (m_values[Amount]);
}

void DeEsserModule::setParameter (int parameterIndex, float value) noexcept
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
        case Amount:  m_amount.setTarget (clamped); break;
        case ReleaseMs: m_releaseDirty = true; break;
        default:      m_coefficientsDirty = true; break;
    }
}

float DeEsserModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void DeEsserModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_coefficientsDirty)
        updateCoefficients();

    if (m_releaseDirty)
        recomputeRelease();

    const float threshold = std::pow (10.0f, m_values[ThresholdDb] / 20.0f);

    for (int channel = 0; channel < numChannels && channel < 2; ++channel)
    {
        float* data = channels[channel];

        if (data == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];

            float low[1] { input };
            float* lowChannel[1] { low };
            m_lowPass.process (lowChannel, 1, 1);

            // Complementary split: low + high is exactly the input, so an idle
            // module cannot colour the signal.
            const float high = input - low[0];
            const float magnitude = std::abs (high);
            const float amount = m_amount.next();

            const float attacked = magnitude > m_envelope[channel]
                                       ? magnitude
                                       : m_envelope[channel] * m_releaseCoefficient;
            m_envelope[channel] = flushDenormal (attacked);

            float target = 1.0f;

            if (m_envelope[channel] > threshold && m_envelope[channel] > 1.0e-9f)
            {
                const float ratio = threshold / m_envelope[channel];
                target = std::pow (ratio, 1.0f + amount * 3.0f);
            }

            // Smooth the applied gain so a fast envelope does not modulate the
            // band into a click.
            m_gain[channel] += 0.01f * (target - m_gain[channel]);

            data[i] = low[0] + high * m_gain[channel];
        }
    }
}

} // namespace zfactory
