#include "TapeModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.saturation.tape";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Tape";
    manifest.description = "Tape-machine character: head bump, biased asymmetry and high-frequency loss";
    manifest.category = ModuleCategory::Saturation;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.0f;

    {
        ParameterManifest parameter;
        parameter.id = "driveDb";
        parameter.displayName = "Drive";
        parameter.unit = "dB";
        parameter.minValue = 0.0f;
        parameter.maxValue = 18.0f;
        parameter.defaultValue = 3.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "bias";
        parameter.displayName = "Bias";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.25f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Character";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "bumpDb";
        parameter.displayName = "Head Bump";
        parameter.unit = "dB";
        parameter.minValue = 0.0f;
        parameter.maxValue = 6.0f;
        parameter.defaultValue = 2.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Tone";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "hfHz";
        parameter.displayName = "HF Loss";
        parameter.unit = "Hz";
        parameter.minValue = 2000.0f;
        parameter.maxValue = 20000.0f;
        parameter.defaultValue = 12000.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Tone";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "mix";
        parameter.displayName = "Mix";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 1.0f;
        parameter.smoothingMs = 30.0f;
        parameter.displayGroup = "Output";
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
TapeModule::TapeModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& TapeModule::getManifest() const noexcept
{
    return manifest();
}

void TapeModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);
    m_headBump.prepare (std::max (1, context.numChannels));

    m_coefficientsDirty = true;
    recomputeTargets();

    m_drive.prepare (context.sampleRate, 0.03, m_drive.target());
    m_bias.prepare (context.sampleRate, 0.03, m_bias.target());
    m_hfCoefficient.prepare (context.sampleRate, 0.03, m_hfCoefficient.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());

    reset();
}

void TapeModule::recomputeTargets() noexcept
{
    m_drive.setTarget (std::pow (10.0f, m_values[DriveDb] / 20.0f));
    m_bias.setTarget (m_values[Bias] * 1.2f);

    const float bounded = std::min (m_values[HfHz], m_sampleRate * 0.45f);
    m_hfCoefficient.setTarget (1.0f - std::exp (-2.0f * 3.14159265358979f * bounded / m_sampleRate));

    m_mix.setTarget (m_values[Mix]);

    if (m_coefficientsDirty)
    {
        // Reproduce-head resonance: a gentle low shelf at 80 Hz.
        m_headBump.setCoefficients (
            RBJ::makeLowShelf (m_sampleRate, 80.0f, 0.7f, std::pow (10.0f, m_values[BumpDb] / 20.0f)));
        m_coefficientsDirty = false;
    }
}

void TapeModule::reset() noexcept
{
    recomputeTargets();

    m_headBump.reset();
    m_hfState[0] = 0.0f;
    m_hfState[1] = 0.0f;

    m_drive.setImmediate (m_drive.target());
    m_bias.setImmediate (m_bias.target());
    m_hfCoefficient.setImmediate (m_hfCoefficient.target());
    m_mix.setImmediate (m_mix.target());
}

void TapeModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;

    if (parameterIndex == BumpDb)
        m_coefficientsDirty = true;

    recomputeTargets();
}

float TapeModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void TapeModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    for (int channel = 0; channel < numChannels && channel < 2; ++channel)
    {
        float* data = channels[channel];

        if (data == nullptr)
            continue;

        for (int i = 0; i < numSamples; ++i)
        {
            const float drive = m_drive.next();
            const float bias = m_bias.next();
            const float hf = m_hfCoefficient.next();
            const float mix = m_mix.next();

            const float dry = data[i];
            const float dc = std::tanh (bias);

            // Full-scale input maps to full-scale output whatever the drive, so
            // the control changes character instead of loudness. The divisor is
            // the LARGER of the two half-scale excursions: a bias makes the
            // positive and negative halves compress differently (that is the
            // even-harmonic asymmetry tape is wanted for), but normalising each
            // half by its own limit would cancel the asymmetry and normalising
            // by the positive half alone lets a large bias make the negative
            // half tens of dB louder than full scale.
            const float positive = std::tanh (drive + bias) - dc;
            const float negative = dc - std::tanh (-drive + bias);
            const float fullScale = std::max (1.0e-3f, std::max (positive, negative));

            float wet = (std::tanh (dry * drive + bias) - dc) / fullScale;

            // The reproduce head reads the recorded tape, so its resonance
            // belongs after the saturation - and inside the wet path, so that
            // mix 0 is a true pass-through.
            wet = m_headBump.processSample (channel, wet);

            m_hfState[channel] += hf * (wet - m_hfState[channel]);
            wet = m_hfState[channel];

            data[i] = dry * (1.0f - mix) + wet * mix;
        }
    }
}

} // namespace zfactory
