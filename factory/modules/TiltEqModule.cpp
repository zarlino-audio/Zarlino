#include "TiltEqModule.h"

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.eq.tilt";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Tilt EQ";
    manifest.description = "Spectral tilt around a pivot frequency";
    manifest.category = ModuleCategory::Equalizer;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.4f;

    {
        ParameterManifest parameter;
        parameter.id = "tiltDb";
        parameter.displayName = "Tilt";
        parameter.unit = "dB";
        parameter.minValue = -12.0f;
        parameter.maxValue = 12.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Tone";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "pivotHz";
        parameter.displayName = "Pivot";
        parameter.unit = "Hz";
        parameter.minValue = 200.0f;
        parameter.maxValue = 8000.0f;
        parameter.defaultValue = 1000.0f;
        parameter.logarithmic = true;
        parameter.displayGroup = "Tone";
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
TiltEqModule::TiltEqModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TiltEqModule::getManifest() const noexcept
{
    return manifest();
}

void TiltEqModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);
    m_lowShelf.prepare (std::max (1, context.numChannels));
    m_highShelf.prepare (std::max (1, context.numChannels));
    m_coefficientsDirty = true;
    updateCoefficients();
}

void TiltEqModule::updateCoefficients() noexcept
{
    const float nyquist = m_sampleRate * 0.5f;
    const float pivot = std::min (m_values[PivotHz], nyquist * 0.9f);
    const float halfTilt = m_values[TiltDb] * 0.5f;

    m_lowShelf.setCoefficients (
        RBJ::makeLowShelf (m_sampleRate, pivot, 0.70710678f, zdsp::decibelsToGain (-halfTilt)));

    m_highShelf.setCoefficients (
        RBJ::makeHighShelf (m_sampleRate, pivot, 0.70710678f, zdsp::decibelsToGain (halfTilt)));

    m_coefficientsDirty = false;
}

void TiltEqModule::reset() noexcept
{
    m_lowShelf.reset();
    m_highShelf.reset();
}

void TiltEqModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;
    m_coefficientsDirty = true;
}

float TiltEqModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TiltEqModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_coefficientsDirty)
        updateCoefficients();

    m_lowShelf.process (channels, numChannels, numSamples);
    m_highShelf.process (channels, numChannels, numSamples);
}

} // namespace zfactory
