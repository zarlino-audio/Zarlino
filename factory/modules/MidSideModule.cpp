#include "MidSideModule.h"

#include <algorithm>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.stereo.midside";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Mid/Side Width";
    manifest.description = "Mid/side width with a bass-mono guard";
    manifest.category = ModuleCategory::Stereo;
    manifest.certification = CertificationState::Certified;
    // Pure mid/side mathematics: a mono signal has no side component, so the
    // module is only meaningful on a stereo bus.
    manifest.supportedChannels = { ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.7f;

    {
        ParameterManifest parameter;
        parameter.id = "width";
        parameter.displayName = "Width";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 2.0f;
        parameter.defaultValue = 1.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Width";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "bassMonoHz";
        parameter.displayName = "Bass Mono";
        parameter.unit = "Hz";
        parameter.minValue = 20.0f;
        parameter.maxValue = 500.0f;
        parameter.defaultValue = 20.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Width";
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
MidSideModule::MidSideModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& MidSideModule::getManifest() const noexcept
{
    return manifest();
}

void MidSideModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);
    m_sideHighPass.prepare (2);
    m_coefficientsDirty = true;
    updateCoefficients();

    m_width.prepare (context.sampleRate, 0.03, m_values[Width]);

    reset();
}

void MidSideModule::updateCoefficients() noexcept
{
    const float nyquist = m_sampleRate * 0.5f;
    const float corner = std::min (m_values[BassMonoHz], nyquist * 0.5f);

    // 12 dB/oct is enough: the guard only has to remove side energy, not define
    // an audible crossover.
    m_sideHighPass.setCoefficients (RBJ::makeHighPass (m_sampleRate, corner, 0.70710678f));
    m_coefficientsDirty = false;
}

void MidSideModule::reset() noexcept
{
    m_sideHighPass.reset();
    m_width.setImmediate (m_values[Width]);
}

void MidSideModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;

    if (parameterIndex == Width)
    {
        m_width.setTarget (clamped);
    }
    else
    {
        m_coefficientsDirty = true;
    }
}

float MidSideModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void MidSideModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    // A mono buffer has no side channel to manipulate; leaving it untouched is
    // the only correct answer, and it is what a host in mono expects.
    if (numChannels < 2)
        return;

    if (m_coefficientsDirty)
        updateCoefficients();

    float* left = channels[0];
    float* right = channels[1];

    for (int i = 0; i < numSamples; ++i)
    {
        const float mid = (left[i] + right[i]) * 0.5f;
        const float side = (left[i] - right[i]) * 0.5f;

        float filteredSide[1] { side };
        float* sideChannel[1] { filteredSide };
        m_sideHighPass.process (sideChannel, 1, 1);

        const float width = m_width.next();
        const float scaled = filteredSide[0] * width;

        left[i] = mid + scaled;
        right[i] = mid - scaled;
    }
}

} // namespace zfactory
