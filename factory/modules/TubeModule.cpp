#include "TubeModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation.tube";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Tube";
    manifest.description = "Driven tube stage with asymmetric bias and supply sag";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.8f;

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
        parameter.id = "bias";
        parameter.displayName = "Bias";
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
        parameter.id = "sag";
        parameter.displayName = "Sag";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.3f;
        parameter.smoothingMs = 40.0f;
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
TubeModule::TubeModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TubeModule::getManifest() const noexcept
{
    return manifest();
}

void TubeModule::prepare (const PrepareContext& context)
{
    m_sampleRate = std::max (1.0f, static_cast<float> (context.sampleRate));
    recomputeTargets();

    m_drive.prepare (context.sampleRate, 0.03, m_drive.target());
    m_bias.prepare (context.sampleRate, 0.03, m_bias.target());
    m_sag.prepare (context.sampleRate, 0.04, m_sag.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void TubeModule::recomputeTargets() noexcept
{
    m_drive.setTarget (std::pow (10.0f, m_values[DriveDb] / 20.0f));
    m_bias.setTarget (m_values[Bias] * 1.5f);
    m_sag.setTarget (m_values[Sag]);
    m_mix.setTarget (m_values[Mix]);
}

void TubeModule::reset() noexcept
{
    recomputeTargets();
    m_sagState = 0.0f;
    m_drive.setImmediate (m_drive.target());
    m_bias.setImmediate (m_bias.target());
    m_sag.setImmediate (m_sag.target());
    m_mix.setImmediate (m_mix.target());
}

void TubeModule::setParameter (int parameterIndex, float value) noexcept
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

float TubeModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TubeModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    // Sag is a supply property of the stage, not of a channel: both channels
    // share one follower, which is also what keeps a stereo image from pumping
    // independently.
    const float sagRecovery = std::exp (-1.0f / (0.080f * m_sampleRate));

    for (int i = 0; i < numSamples; ++i)
    {
        const float drive = m_drive.next();
        const float bias = m_bias.next();
        const float sag = m_sag.next();
        const float mix = m_mix.next();

        const float dc = std::tanh (bias);
        const float fullScale = std::max (1.0e-3f, std::tanh (drive + bias) - dc);

        float blockPeak = 0.0f;

        for (int channel = 0; channel < numChannels && channel < 2; ++channel)
        {
            float* data = channels[channel];

            if (data == nullptr)
                continue;

            blockPeak = std::max (blockPeak, std::abs (data[i]));
        }

        // A transient pulls the operating point down; it recovers over ~80 ms.
        m_sagState = std::max (blockPeak, m_sagState * sagRecovery);
        const float sagGain = 1.0f - sag * 0.35f * std::min (1.0f, m_sagState);

        for (int channel = 0; channel < numChannels && channel < 2; ++channel)
        {
            float* data = channels[channel];

            if (data == nullptr)
                continue;

            const float dry = data[i];
            const float wet = ((std::tanh (dry * drive + bias) - dc) / fullScale) * sagGain;
            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

} // namespace zfactory
