#include "DryWetModule.h"

#include <cstring>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.mix.drywet";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Dry/Wet";
    manifest.description = "Parallel dry/wet blend, linear or equal-power";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.2f;

    {
        ParameterManifest parameter;
        parameter.id = "mix";
        parameter.displayName = "Mix";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 1.0f;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Blend";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "equalPower";
        parameter.displayName = "Equal Power";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 2;
        parameter.displayGroup = "Blend";
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
DryWetModule::DryWetModule()
{
    m_values[Mix] = manifest().parameters[Mix].defaultValue;
    m_values[EqualPower] = manifest().parameters[EqualPower].defaultValue;
    m_equalPower = m_values[EqualPower] >= 0.5f;
}

const ModuleManifest& DryWetModule::getManifest() const noexcept
{
    return manifest();
}

void DryWetModule::prepare (const PrepareContext& context)
{
    m_channelCount = std::max (1, context.numChannels);
    m_maxBlockSize = std::max (1, context.maxBlockSize);

    m_dryBuffer.assign (static_cast<size_t> (m_channelCount) * static_cast<size_t> (m_maxBlockSize),
                        0.0f);

    m_mix.prepare (context.sampleRate, 0.02, m_values[Mix]);
    m_equalPower = m_values[EqualPower] >= 0.5f;
}

void DryWetModule::reset() noexcept
{
    std::fill (m_dryBuffer.begin(), m_dryBuffer.end(), 0.0f);
    m_mix.reset (m_values[Mix]);
}

void DryWetModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);
    m_values[parameterIndex] = clamped;

    if (parameterIndex == Mix)
        m_mix.setTarget (clamped);
    else
        m_equalPower = clamped >= 0.5f;
}

float DryWetModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void DryWetModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    const int activeChannels = std::min (numChannels, m_channelCount);

    if (m_dryBuffer.empty() || numSamples > m_maxBlockSize || activeChannels <= 0)
        return;

    for (int ch = 0; ch < activeChannels; ++ch)
        if (channels[ch] != nullptr)
            std::memcpy (m_dryBuffer.data() + static_cast<size_t> (ch) * static_cast<size_t> (m_maxBlockSize),
                         channels[ch],
                         sizeof (float) * static_cast<size_t> (numSamples));

    m_mix.setTarget (m_values[Mix]);

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = clamp01 (m_mix.next());
        const float dryGain = m_equalPower ? std::cos (mix * 1.5707963f) : 1.0f - mix;
        const float wetGain = m_equalPower ? std::sin (mix * 1.5707963f) : mix;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            float* data = channels[ch];

            if (data == nullptr)
                continue;

            const float* dry = m_dryBuffer.data()
                                   + static_cast<size_t> (ch) * static_cast<size_t> (m_maxBlockSize);

            data[i] = flushDenormal (dry[i] * dryGain + data[i] * wetGain);
        }
    }
}

} // namespace zfactory
