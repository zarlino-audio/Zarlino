#include "CompressorModule.h"

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
    manifest.id = "zarlino.dynamics.compressor";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Compressor";
    manifest.description = "Feedforward compressor with soft knee and makeup gain";
    manifest.category = ModuleCategory::Dynamics;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.0f;

    manifest.parameters.push_back (makeParameter ("threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f, false, "Dynamics"));
    manifest.parameters.push_back (makeParameter ("ratio", "Ratio", "ratio", 1.0f, 20.0f, 4.0f, false, "Dynamics"));
    manifest.parameters.push_back (makeParameter ("attack", "Attack", "ms", 0.1f, 200.0f, 10.0f, true, "Timing"));
    manifest.parameters.push_back (makeParameter ("release", "Release", "ms", 10.0f, 2000.0f, 150.0f, true, "Timing"));
    manifest.parameters.push_back (makeParameter ("knee", "Knee", "dB", 0.0f, 24.0f, 6.0f, false, "Dynamics"));
    manifest.parameters.push_back (makeParameter ("makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f, false, "Output"));
    manifest.parameters.push_back (makeParameter ("detection", "Detection", "", 0.0f, 1.0f, 0.0f, false, "Detector"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
CompressorModule::CompressorModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;

    m_peakMode = m_values[Detection] >= 0.5f;
}

const ModuleManifest& CompressorModule::getManifest() const noexcept
{
    return manifest();
}

void CompressorModule::prepare (const PrepareContext& context)
{
    m_appliedChannels = std::min (kMaxChannels, std::max (1, context.numChannels));

    for (int ch = 0; ch < m_appliedChannels; ++ch)
        m_compressors[static_cast<size_t> (ch)].prepare (context.sampleRate);

    m_dirty = true;
    applyParameters();
}

void CompressorModule::applyParameters() noexcept
{
    for (int ch = 0; ch < m_appliedChannels; ++ch)
    {
        auto& compressor = m_compressors[static_cast<size_t> (ch)];

        compressor.setPeakMode (m_peakMode);
        compressor.setThreshold (m_values[Threshold]);
        compressor.setRatio (m_values[Ratio]);
        compressor.setAttack (m_values[Attack]);
        compressor.setRelease (m_values[Release]);
        compressor.setKnee (m_values[Knee]);
        compressor.setMakeupGain (m_values[Makeup]);
    }

    m_dirty = false;
}

void CompressorModule::reset() noexcept
{
    for (int ch = 0; ch < m_appliedChannels; ++ch)
        m_compressors[static_cast<size_t> (ch)].reset();
}

void CompressorModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;

    if (parameterIndex == Detection)
        m_peakMode = clamped >= 0.5f;

    m_dirty = true;
}

float CompressorModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void CompressorModule::process (float* const* channels, int numChannels, int numSamples) noexcept
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

        auto& compressor = m_compressors[static_cast<size_t> (ch)];

        for (int i = 0; i < numSamples; ++i)
            data[i] = flushDenormal (compressor.processSample (data[i]));
    }
}

} // namespace zfactory
