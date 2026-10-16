#include "FilterModule.h"

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.filter.multimode";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Multimode Filter";
    manifest.description = "Resonant low-pass / high-pass, 12 or 24 dB per octave";
    manifest.category = ModuleCategory::Filter;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 0.4f;

    {
        ParameterManifest parameter;
        parameter.id = "shape";
        parameter.displayName = "Shape";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 2;
        parameter.displayGroup = "Filter";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "frequency";
        parameter.displayName = "Frequency";
        parameter.unit = "Hz";
        parameter.minValue = 20.0f;
        parameter.maxValue = 20000.0f;
        parameter.defaultValue = 1000.0f;
        parameter.logarithmic = true;
        parameter.displayGroup = "Filter";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "resonance";
        parameter.displayName = "Resonance";
        parameter.unit = "Q";
        parameter.minValue = 0.1f;
        parameter.maxValue = 10.0f;
        parameter.defaultValue = 0.707f;
        parameter.logarithmic = true;
        parameter.displayGroup = "Filter";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "slope";
        parameter.displayName = "Slope";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 2;
        parameter.displayGroup = "Filter";
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
FilterModule::FilterModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;

    m_highPass = m_values[Shape] >= 0.5f;
    m_twoSections = m_values[Slope] >= 0.5f;
}

const ModuleManifest& FilterModule::getManifest() const noexcept
{
    return manifest();
}

void FilterModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);

    // Preparing here (rather than relying on MultiChannelBiquad's lazy prepare)
    // is what keeps process() allocation free.
    m_sectionA.prepare (std::max (1, context.numChannels));
    m_sectionB.prepare (std::max (1, context.numChannels));

    m_coefficientsDirty = true;
    updateCoefficients();
}

void FilterModule::updateCoefficients() noexcept
{
    const float nyquist = m_sampleRate * 0.5f;
    const float frequency = std::min (m_values[Frequency], nyquist * 0.98f);
    const float q = std::max (0.1f, m_values[Resonance]);

    m_pendingA = m_highPass ? RBJ::makeHighPass (m_sampleRate, frequency, q)
                            : RBJ::makeLowPass (m_sampleRate, frequency, q);

    // The second section is a Butterworth-matched cascade: Q is scaled so the
    // 24 dB/oct response keeps the same -3 dB corner as the 12 dB/oct one.
    const float cascadeQ = std::max (0.1f, q * 0.5412f);

    m_pendingB = m_highPass ? RBJ::makeHighPass (m_sampleRate, frequency, cascadeQ)
                            : RBJ::makeLowPass (m_sampleRate, frequency, cascadeQ);

    m_sectionA.setCoefficients (m_pendingA);
    m_sectionB.setCoefficients (m_pendingB);
    m_coefficientsDirty = false;
}

void FilterModule::reset() noexcept
{
    m_sectionA.reset();
    m_sectionB.reset();
}

void FilterModule::setParameter (int parameterIndex, float value) noexcept
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
        case Shape:     m_highPass = clamped >= 0.5f;     break;
        case Slope:     m_twoSections = clamped >= 0.5f;  break;
        default:                                          break;
    }

    m_coefficientsDirty = true;
}

float FilterModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void FilterModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_coefficientsDirty)
        updateCoefficients();

    m_sectionA.process (channels, numChannels, numSamples);

    if (m_twoSections)
        m_sectionB.process (channels, numChannels, numSamples);
}

} // namespace zfactory
