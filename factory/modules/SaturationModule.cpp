#include "SaturationModule.h"

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
                                 bool stepped,
                                 int stepCount,
                                 float smoothingMs,
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
    parameter.stepped = stepped;
    parameter.stepCount = stepCount;
    parameter.smoothingMs = smoothingMs;
    parameter.displayGroup = group;
    return parameter;
}

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Saturation";
    manifest.description = "Six-mode harmonic saturator with ADAA anti-aliasing (ZScorch curves)";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.6f;

    manifest.parameters.push_back (makeParameter ("drive", "Drive", "", 0.0f, 1.0f, 0.0f, false, false, 0, 20.0f, "Drive"));
    manifest.parameters.push_back (makeParameter ("mode", "Mode", "", 0.0f, 5.0f, 0.0f, false, true, 6, 0.0f, "Drive"));
    manifest.parameters.push_back (makeParameter ("harmReduction", "Harmonic Reduction", "", 0.0f, 1.0f, 0.0f, false, false, 0, 20.0f, "Tone"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
SaturationModule::SaturationModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& SaturationModule::getManifest() const noexcept
{
    return manifest();
}

SaturationMode SaturationModule::toMode (float value) noexcept
{
    const int index = std::min (kNumModes - 1,
                                std::max (0, static_cast<int> (std::lround (value))));

    return static_cast<SaturationMode> (index);
}

void SaturationModule::prepare (const PrepareContext& context)
{
    m_preparedChannels = std::max (1, context.numChannels);

    m_saturator.prepare (context.sampleRate, std::max (1, context.maxBlockSize), m_preparedChannels);

    // A FRESH owned buffer, not setSize(): after a process() call this buffer is
    // a non-owning view over the caller's channel data, and setSize() would be a
    // no-op because the shape has not changed - leaving the view dangling. Move
    // assignment gives the module its own storage back.
    m_view = juce::AudioBuffer<float> (m_preparedChannels, std::max (1, context.maxBlockSize));
    m_view.clear();
}

void SaturationModule::reset() noexcept
{
    // Only the saturator's own state is ours to clear. m_view may still be
    // pointing at the buffers the caller handed us for the previous block, so it
    // must never be written or resized here.
    m_saturator.reset();
}

int SaturationModule::getLatencySamples() const noexcept
{
    return m_saturator.getLatencySamples();
}

void SaturationModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    m_values[parameterIndex] = domain.clampValue (value);
}

float SaturationModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void SaturationModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    const int activeChannels = std::min (numChannels, m_preparedChannels);

    if (activeChannels <= 0 || numSamples <= 0 || numSamples > m_view.getNumSamples())
        return;

    // Non-owning view over the caller's buffers: activeChannels never exceeds
    // the prepared channel count, so setDataToReferTo cannot reallocate.
    m_view.setDataToReferTo (channels, activeChannels, numSamples);

    m_saturator.process (m_view,
                         m_values[Drive],
                         toMode (m_values[Mode]),
                         m_values[HarmReduction]);
}

} // namespace zfactory
