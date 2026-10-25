#include "PhaserModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.modulation.phaser";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Phaser";
    manifest.description = "Six-stage swept allpass chain for swirling phase movement";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.8f;

    {
        ParameterManifest parameter;
        parameter.id = "rateHz";
        parameter.displayName = "Rate";
        parameter.unit = "Hz";
        parameter.minValue = 0.05f;
        parameter.maxValue = 8.0f;
        parameter.defaultValue = 0.4f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 50.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "centreHz";
        parameter.displayName = "Centre";
        parameter.unit = "Hz";
        parameter.minValue = 150.0f;
        parameter.maxValue = 3000.0f;
        parameter.defaultValue = 700.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 60.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "depth";
        parameter.displayName = "Depth";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.7f;
        parameter.smoothingMs = 50.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "feedback";
        parameter.displayName = "Feedback";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 0.9f;
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
        parameter.defaultValue = 0.5f;
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
PhaserModule::PhaserModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& PhaserModule::getManifest() const noexcept
{
    return manifest();
}

void PhaserModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);

    for (int channel = 0; channel < 2; ++channel)
        m_lfo[channel].prepare (context.sampleRate);

    recomputeTargets();

    m_rate.prepare (context.sampleRate, 0.05, m_rate.target());
    m_depth.prepare (context.sampleRate, 0.05, m_depth.target());
    m_feedback.prepare (context.sampleRate, 0.04, m_feedback.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void PhaserModule::recomputeTargets() noexcept
{
    m_rate.setTarget (m_values[RateHz]);
    m_depth.setTarget (m_values[Depth]);
    m_feedback.setTarget (std::min (0.9f, m_values[Feedback]));
    m_mix.setTarget (m_values[Mix]);
    m_targetsDirty = false;
}

void PhaserModule::reset() noexcept
{
    recomputeTargets();

    for (auto& channel : m_state)
        for (auto& stage : channel)
            stage = 0.0f;

    m_feedbackState[0] = 0.0f;
    m_feedbackState[1] = 0.0f;

    for (int channel = 0; channel < 2; ++channel)
        m_lfo[channel].reset (channel == 0 ? 0.0f : 0.5f);

    m_rate.setImmediate (m_rate.target());
    m_depth.setImmediate (m_depth.target());
    m_feedback.setImmediate (m_feedback.target());
    m_mix.setImmediate (m_mix.target());
}

void PhaserModule::setParameter (int parameterIndex, float value) noexcept
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

float PhaserModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

float PhaserModule::allpassCoefficient (float frequencyHz) const noexcept
{
    // First-order allpass: a = (t - 1) / (t + 1), t = tan(pi f / fs).
    const float bounded = std::max (20.0f, std::min (frequencyHz, m_sampleRate * 0.45f));
    const float t = std::tan (3.14159265358979f * bounded / m_sampleRate);
    return (t - 1.0f) / (t + 1.0f);
}

void PhaserModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_targetsDirty)
        recomputeTargets();

    const bool stereo = numChannels > 1;
    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const float rate = m_rate.next();
        const float depth = m_depth.next();
        const float feedback = m_feedback.next();
        const float mix = m_mix.next();

        m_lfo[0].setRate (rate);
        m_lfo[1].setRate (rate);

        const float dryLeft = left[i];
        const float dryRight = right[i];

        float wet[2] { 0.0f, 0.0f };
        const float input[2] { dryLeft, dryRight };

        for (int channel = 0; channel < 2; ++channel)
        {
            const float sweep = m_lfo[channel].next (0);
            const float frequency = m_values[CentreHz] * std::pow (2.0f, depth * 2.0f * sweep);
            const float a = allpassCoefficient (frequency);

            float value = input[channel] + m_feedbackState[channel] * feedback;

            for (int stage = 0; stage < kStages; ++stage)
            {
                float& state = m_state[channel][stage];
                const float output = a * value + state;
                state = flushDenormal (value - a * output);
                value = output;
            }

            m_feedbackState[channel] = flushDenormal (value);
            wet[channel] = value;
        }

        left[i] = dryLeft * (1.0f - mix) + wet[0] * mix;
        right[i] = dryRight * (1.0f - mix) + wet[1] * mix;
    }
}

} // namespace zfactory
