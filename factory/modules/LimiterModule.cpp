#include "LimiterModule.h"

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
    manifest.id = "zarlino.limiter";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Limiter";
    manifest.description = "Brick-wall lookahead limiter with a fixed 1 ms window";
    manifest.category = ModuleCategory::Limiter;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };

    // 1 ms at the 48 kHz reference rate; the module reports the exact figure for
    // the prepared rate.
    manifest.referenceLatencySamples = 48;
    manifest.latencySampleRateDependent = true;
    manifest.cpuCostPerSample = 0.8f;

    manifest.parameters.push_back (makeParameter ("ceiling", "Ceiling", "dB", -12.0f, 0.0f, -1.0f, false, "Limiter"));
    manifest.parameters.push_back (makeParameter ("release", "Release", "ms", 1.0f, 500.0f, 50.0f, true, "Limiter"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}
} // namespace

//==============================================================================
LimiterModule::LimiterModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& LimiterModule::getManifest() const noexcept
{
    return manifest();
}

void LimiterModule::prepare (const PrepareContext& context)
{
    m_appliedChannels = std::min (kMaxChannels, std::max (1, context.numChannels));

    m_limiter.prepare (context.sampleRate, kLookaheadMs, m_appliedChannels);
    m_latencySamples = m_limiter.getLatencySamples();

    m_dirty = true;
    applyParameters();
}

void LimiterModule::applyParameters() noexcept
{
    m_limiter.setCeiling (m_values[Ceiling]);
    m_limiter.setRelease (m_values[Release]);
    m_dirty = false;
}

void LimiterModule::reset() noexcept
{
    m_limiter.reset();
}

void LimiterModule::setParameter (int parameterIndex, float value) noexcept
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

float LimiterModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void LimiterModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_dirty)
        applyParameters();

    const int activeChannels = std::min (numChannels, m_appliedChannels);

    // ZLimiter tracks the peak across channels and applies one gain to all of
    // them, so the gain-reduction state must advance in strict sample lockstep:
    // samples outer, channels inner.
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < activeChannels; ++ch)
        {
            float* data = channels[ch];

            if (data == nullptr)
                continue;

            data[i] = flushDenormal (m_limiter.processSample (data[i], ch));
        }
    }
}

} // namespace zfactory
