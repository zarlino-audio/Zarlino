#include "ParametricEqModule.h"

namespace zfactory
{

namespace
{
ParameterManifest makeBandParameter (const char* id,
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
    manifest.id = "zarlino.eq.parametric";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Parametric EQ";
    manifest.description = "Four-band parametric equaliser: low shelf, two peaks, high shelf";
    manifest.category = ModuleCategory::Equalizer;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.2f;

    manifest.parameters.push_back (makeBandParameter ("lowShelfFreq", "Low Shelf Freq", "Hz", 20.0f, 1000.0f, 100.0f, true, "Low"));
    manifest.parameters.push_back (makeBandParameter ("lowShelfGain", "Low Shelf Gain", "dB", -18.0f, 18.0f, 0.0f, false, "Low"));
    manifest.parameters.push_back (makeBandParameter ("lowShelfQ", "Low Shelf Q", "Q", 0.3f, 4.0f, 0.707f, true, "Low"));

    manifest.parameters.push_back (makeBandParameter ("peakLowFreq", "Low Peak Freq", "Hz", 60.0f, 8000.0f, 400.0f, true, "Low Mid"));
    manifest.parameters.push_back (makeBandParameter ("peakLowGain", "Low Peak Gain", "dB", -18.0f, 18.0f, 0.0f, false, "Low Mid"));
    manifest.parameters.push_back (makeBandParameter ("peakLowQ", "Low Peak Q", "Q", 0.3f, 8.0f, 0.9f, true, "Low Mid"));

    manifest.parameters.push_back (makeBandParameter ("peakHighFreq", "High Peak Freq", "Hz", 300.0f, 16000.0f, 3000.0f, true, "High Mid"));
    manifest.parameters.push_back (makeBandParameter ("peakHighGain", "High Peak Gain", "dB", -18.0f, 18.0f, 0.0f, false, "High Mid"));
    manifest.parameters.push_back (makeBandParameter ("peakHighQ", "High Peak Q", "Q", 0.3f, 8.0f, 0.9f, true, "High Mid"));

    manifest.parameters.push_back (makeBandParameter ("highShelfFreq", "High Shelf Freq", "Hz", 2000.0f, 20000.0f, 8000.0f, true, "High"));
    manifest.parameters.push_back (makeBandParameter ("highShelfGain", "High Shelf Gain", "dB", -18.0f, 18.0f, 0.0f, false, "High"));
    manifest.parameters.push_back (makeBandParameter ("highShelfQ", "High Shelf Q", "Q", 0.3f, 4.0f, 0.707f, true, "High"));

    return manifest;
}

const ModuleManifest& manifest()
{
    static const ModuleManifest instance = makeManifest();
    return instance;
}

ZEqualizerBandType bandType (int band)
{
    switch (band)
    {
        case 0:  return ZEqualizerBandType::LowShelf;
        case 3:  return ZEqualizerBandType::HighShelf;
        default: return ZEqualizerBandType::Peak;
    }
}
} // namespace

//==============================================================================
ParametricEqModule::ParametricEqModule()
{
    const auto& parameters = manifest().parameters;

    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = parameters[static_cast<size_t> (i)].defaultValue;

    for (int band = 0; band < kNumBands; ++band)
        m_dirty[band] = true;
}

const ModuleManifest& ParametricEqModule::getManifest() const noexcept
{
    return manifest();
}

void ParametricEqModule::pushBand (int band) noexcept
{
    const int base = band * 3;

    ZEqualizerBandParams params;
    params.frequency = m_values[base + 0];
    params.gainDb = m_values[base + 1];
    params.Q = std::max (0.05f, m_values[base + 2]);
    params.bypass = false;
    params.type = bandType (band);

    m_equalizer.setBandParams (band, params);
    m_dirty[band] = false;
}

void ParametricEqModule::prepare (const PrepareContext& context)
{
    m_equalizer.prepare (context.sampleRate, std::max (1, context.numChannels));

    for (int band = 0; band < kNumBands; ++band)
        pushBand (band);
}

void ParametricEqModule::reset() noexcept
{
    m_equalizer.reset();
}

void ParametricEqModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;
    m_dirty[parameterIndex / 3] = true;
}

float ParametricEqModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void ParametricEqModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    for (int band = 0; band < kNumBands; ++band)
        if (m_dirty[band])
            pushBand (band);

    m_equalizer.process (channels, numChannels, numSamples);
}

} // namespace zfactory
