#include "TransientShaperModule.h"

namespace zfactory
{

namespace
{
ParameterManifest makeParameter (const char* id,
                                 const char* displayName,
                                 const char* unit,
                                 float minValue,
                                 float maxValue,
                                 float defaultValue,
                                 bool logarithmic,
                                 const char* group)
{
    ParameterManifest parameter;
    parameter.id = id;
    parameter.displayName = displayName;
    parameter.unit = unit;
    parameter.minValue = minValue;
    parameter.maxValue = maxValue;
    parameter.defaultValue = defaultValue;
    parameter.logarithmic = logarithmic;
    parameter.displayGroup = group;
    return parameter;
}

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.shaping.transient";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Transient Shaper";
    manifest.description = "Dual-envelope transient shaper for attack and sustain";
    manifest.category = ModuleCategory::Shaping;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.1f;

    manifest.parameters.push_back (makeParameter ("attack", "Attack", "dB", -24.0f, 24.0f, 0.0f, false, "Shaper"));
    manifest.parameters.push_back (makeParameter ("sustain", "Sustain", "dB", -24.0f, 24.0f, 0.0f, false, "Shaper"));
    manifest.parameters.push_back (makeParameter ("attackTime", "Attack Time", "ms", 0.5f, 50.0f, 5.0f, true, "Detector"));
    manifest.parameters.push_back (makeParameter ("releaseTime", "Release Time", "ms", 5.0f, 500.0f, 100.0f, true, "Detector"));
    manifest.parameters.push_back (makeParameter ("sensitivity", "Sensitivity", "", 0.0f, 1.0f, 0.5f, false, "Detector"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
TransientShaperModule::TransientShaperModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TransientShaperModule::getManifest() const noexcept
{
    return manifest();
}

void TransientShaperModule::prepare (const PrepareContext& context)
{
    m_appliedChannels = std::min (kMaxChannels, std::max (1, context.numChannels));

    for (int ch = 0; ch < m_appliedChannels; ++ch)
    {
        m_fastEnvelope[static_cast<size_t> (ch)].prepare (context.sampleRate);
        m_slowEnvelope[static_cast<size_t> (ch)].prepare (context.sampleRate);
        m_fastEnvelope[static_cast<size_t> (ch)].setPeakMode (false);
        m_slowEnvelope[static_cast<size_t> (ch)].setPeakMode (false);
    }

    m_dirty = true;
    applyParameters();
}

void TransientShaperModule::applyParameters() noexcept
{
    const float attackMs = m_values[AttackTime];
    const float releaseMs = m_values[ReleaseTime];

    for (int ch = 0; ch < m_appliedChannels; ++ch)
    {
        auto& fast = m_fastEnvelope[static_cast<size_t> (ch)];
        auto& slow = m_slowEnvelope[static_cast<size_t> (ch)];

        fast.setAttack (attackMs);
        fast.setRelease (std::max (attackMs, 1.0f));
        slow.setAttack (releaseMs);
        slow.setRelease (releaseMs);
    }

    m_attackGain = zdsp::decibelsToGain (m_values[AttackDb], -40.0f);
    m_sustainGain = zdsp::decibelsToGain (m_values[SustainDb], -40.0f);

    // 0.25 .. 2.0: how strongly the detector balance is pushed towards "attack".
    m_emphasis = 0.25f + 1.75f * std::min (1.0f, std::max (0.0f, m_values[Sensitivity]));

    m_dirty = false;
}

void TransientShaperModule::reset() noexcept
{
    for (int ch = 0; ch < m_appliedChannels; ++ch)
    {
        m_fastEnvelope[static_cast<size_t> (ch)].reset();
        m_slowEnvelope[static_cast<size_t> (ch)].reset();
    }
}

void TransientShaperModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;
    m_dirty = true;
}

float TransientShaperModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TransientShaperModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_dirty)
        applyParameters();

    const int activeChannels = std::min (numChannels, m_appliedChannels);
    const float emphasis = m_emphasis;
    const float attackGain = m_attackGain;
    const float sustainGain = m_sustainGain;

    for (int ch = 0; ch < activeChannels; ++ch)
    {
        float* data = channels[ch];

        if (data == nullptr)
            continue;

        auto& fast = m_fastEnvelope[static_cast<size_t> (ch)];
        auto& slow = m_slowEnvelope[static_cast<size_t> (ch)];

        for (int i = 0; i < numSamples; ++i)
        {
            const float fastLevel = fast.process (data[i]);
            const float slowLevel = slow.process (data[i]);

            // 0 = the signal is all body, 1 = the signal is all transient.
            const float delta = std::max (0.0f, fastLevel - slowLevel);
            const float denominator = slowLevel + delta;
            const float balance = denominator > 1.0e-12f ? delta / denominator : 0.0f;

            const float weight = std::min (1.0f, balance * emphasis);
            const float gain = sustainGain + (attackGain - sustainGain) * weight;

            data[i] = flushDenormal (data[i] * gain);
        }
    }
}

} // namespace zfactory
