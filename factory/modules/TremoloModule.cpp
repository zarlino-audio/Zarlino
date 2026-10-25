#include "TremoloModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.modulation.tremolo";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Tremolo / Pan";
    manifest.description = "Amplitude modulation with a stereo phase offset for auto-pan";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.6f;

    {
        ParameterManifest parameter;
        parameter.id = "rateHz";
        parameter.displayName = "Rate";
        parameter.unit = "Hz";
        parameter.minValue = 0.05f;
        parameter.maxValue = 20.0f;
        parameter.defaultValue = 4.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
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
        parameter.defaultValue = 0.6f;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "shape";
        parameter.displayName = "Shape";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 2.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 3;
        parameter.displayGroup = "Movement";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "stereoPhase";
        parameter.displayName = "Stereo Phase";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Movement";
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
TremoloModule::TremoloModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TremoloModule::getManifest() const noexcept
{
    return manifest();
}

void TremoloModule::prepare (const PrepareContext& context)
{
    m_sampleRate = std::max (1.0f, static_cast<float> (context.sampleRate));

    m_rate.prepare (context.sampleRate, 0.04, m_values[RateHz]);
    m_depth.prepare (context.sampleRate, 0.04, m_values[Depth]);
    m_phaseOffset.prepare (context.sampleRate, 0.04, m_values[StereoPhase]);

    reset();
}

void TremoloModule::reset() noexcept
{
    m_phase = 0.0f;
    m_rate.setImmediate (m_values[RateHz]);
    m_depth.setImmediate (m_values[Depth]);
    m_phaseOffset.setImmediate (m_values[StereoPhase]);
}

void TremoloModule::setParameter (int parameterIndex, float value) noexcept
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
        case RateHz:      m_rate.setTarget (clamped); break;
        case Depth:       m_depth.setTarget (clamped); break;
        case StereoPhase: m_phaseOffset.setTarget (clamped); break;
        default: break;
    }
}

float TremoloModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TremoloModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    const int shape = static_cast<int> (std::lround (m_values[Shape]));
    const bool stereo = numChannels > 1;

    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const float rate = m_rate.next();
        const float depth = m_depth.next();
        const float offset = m_phaseOffset.next();

        const float leftValue = ModLfo::evaluate (m_phase, shape);

        float rightPhase = m_phase + offset;

        if (rightPhase >= 1.0f)
            rightPhase -= 1.0f;

        const float rightValue = ModLfo::evaluate (rightPhase, shape);

        // Unipolar gain: depth 1 reaches silence at the trough, which is a full
        // tremolo rather than a gentle wobble.
        left[i] *= 1.0f - depth * 0.5f * (1.0f - leftValue);
        right[i] *= 1.0f - depth * 0.5f * (1.0f - rightValue);

        m_phase += rate / m_sampleRate;

        if (m_phase >= 1.0f)
            m_phase -= std::floor (m_phase);
    }
}

} // namespace zfactory
