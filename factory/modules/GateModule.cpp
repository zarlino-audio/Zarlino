#include "GateModule.h"

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
    manifest.id = "zarlino.dynamics.gate";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Gate";
    manifest.description = "Downward expander / noise gate with hysteresis";
    manifest.category = ModuleCategory::Dynamics;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.9f;

    manifest.parameters.push_back (makeParameter ("threshold", "Threshold", "dB", -80.0f, 0.0f, -45.0f, false, "Gate"));
    manifest.parameters.push_back (makeParameter ("range", "Range", "dB", -80.0f, 0.0f, -40.0f, false, "Gate"));
    manifest.parameters.push_back (makeParameter ("attack", "Attack", "ms", 0.1f, 200.0f, 2.0f, true, "Timing"));
    manifest.parameters.push_back (makeParameter ("release", "Release", "ms", 10.0f, 2000.0f, 200.0f, true, "Timing"));
    manifest.parameters.push_back (makeParameter ("hysteresis", "Hysteresis", "dB", 0.0f, 24.0f, 6.0f, false, "Gate"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
GateModule::GateModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& GateModule::getManifest() const noexcept
{
    return manifest();
}

void GateModule::prepare (const PrepareContext& context)
{
    m_appliedChannels = std::min (kMaxChannels, std::max (1, context.numChannels));

    for (int ch = 0; ch < m_appliedChannels; ++ch)
        m_gates[static_cast<size_t> (ch)].prepare (context.sampleRate);

    m_dirty = true;
    applyParameters();
}

void GateModule::applyParameters() noexcept
{
    for (int ch = 0; ch < m_appliedChannels; ++ch)
    {
        auto& gate = m_gates[static_cast<size_t> (ch)];

        gate.setThreshold (m_values[Threshold]);
        gate.setRange (m_values[Range]);
        gate.setAttack (m_values[Attack]);
        gate.setRelease (m_values[Release]);
        gate.setHysteresis (m_values[Hysteresis]);
    }

    m_dirty = false;
}

void GateModule::reset() noexcept
{
    for (int ch = 0; ch < m_appliedChannels; ++ch)
        m_gates[static_cast<size_t> (ch)].reset();
}

void GateModule::setParameter (int parameterIndex, float value) noexcept
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

float GateModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void GateModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_dirty)
        applyParameters();

    const int activeChannels = std::min (numChannels, m_appliedChannels);

    for (int ch = 0; ch < activeChannels; ++ch)
    {
        float* data = channels[ch];

        if (data == nullptr)
            continue;

        auto& gate = m_gates[static_cast<size_t> (ch)];

        for (int i = 0; i < numSamples; ++i)
            data[i] = flushDenormal (gate.processSample (data[i]));
    }
}

} // namespace zfactory
