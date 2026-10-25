#include "ReverbModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
constexpr float kMaxPredelayMs = 200.0f;

/** Freeverb's tunings at 44.1 kHz. They are mutually prime-ish lengths, which
    is what keeps the tail from collapsing into a ringing pitch. */
constexpr int kCombTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
constexpr int kAllpassTuning[4] = { 556, 441, 341, 225 };
constexpr int kStereoSpread = 23;

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.reverb.algorithmic";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Reverb";
    manifest.description = "Algorithmic room: damped comb network with allpass diffusion and predelay";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 4.5f;

    {
        ParameterManifest parameter;
        parameter.id = "predelayMs";
        parameter.displayName = "Predelay";
        parameter.unit = "ms";
        parameter.minValue = 0.0f;
        parameter.maxValue = kMaxPredelayMs;
        parameter.defaultValue = 20.0f;
        parameter.smoothingMs = 50.0f;
        parameter.displayGroup = "Space";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "size";
        parameter.displayName = "Size";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.55f;
        parameter.smoothingMs = 80.0f;
        parameter.displayGroup = "Space";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "decay";
        parameter.displayName = "Decay";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.6f;
        parameter.smoothingMs = 80.0f;
        parameter.displayGroup = "Space";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "damping";
        parameter.displayName = "Damping";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.4f;
        parameter.smoothingMs = 60.0f;
        parameter.displayGroup = "Tone";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "width";
        parameter.displayName = "Width";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.8f;
        parameter.smoothingMs = 40.0f;
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
        parameter.defaultValue = 0.25f;
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
ReverbModule::ReverbModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& ReverbModule::getManifest() const noexcept
{
    return manifest();
}

void ReverbModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);
    const float ratio = m_sampleRate / 44100.0f;

    for (int channel = 0; channel < 2; ++channel)
    {
        for (int i = 0; i < kCombsPerChannel; ++i)
        {
            const int spread = channel == 0 ? 0 : kStereoSpread;
            m_combs[channel][i].resize (static_cast<int> (std::lround ((kCombTuning[i] + spread) * ratio)));
        }

        for (int i = 0; i < kAllpassesPerChannel; ++i)
        {
            const int spread = channel == 0 ? 0 : kStereoSpread;
            m_allpasses[channel][i].resize (static_cast<int> (std::lround ((kAllpassTuning[i] + spread) * ratio)));
        }

        m_predelay[channel].assign (static_cast<size_t> (std::lround (kMaxPredelayMs * 0.001f * m_sampleRate)) + 2, 0.0f);
    }

    m_predelayLength = static_cast<int> (m_predelay[0].size());
    m_predelayIndex = 0;

    recomputeTargets();

    m_feedback.prepare (context.sampleRate, 0.08, m_feedback.target());
    m_damping.prepare (context.sampleRate, 0.06, m_damping.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());
    m_width.prepare (context.sampleRate, 0.04, m_width.target());

    reset();
}

void ReverbModule::recomputeTargets() noexcept
{
    // Freeverb's mapping: never quite unity (which would never decay) and never
    // so low that a "long hall" request comes back as a short room.
    const float size = m_values[Size];
    const float decay = m_values[Decay];
    m_feedback.setTarget (std::min (0.96f, 0.68f + 0.28f * size) * (0.6f + 0.4f * decay));
    m_damping.setTarget (std::min (0.95f, m_values[Damping]));
    m_mix.setTarget (m_values[Mix]);
    m_width.setTarget (m_values[Width]);
    m_targetsDirty = false;
}

void ReverbModule::reset() noexcept
{
    recomputeTargets();

    for (int channel = 0; channel < 2; ++channel)
    {
        for (auto& comb : m_combs[channel])
            comb.clear();

        for (auto& allpass : m_allpasses[channel])
            allpass.clear();

        std::fill (m_predelay[channel].begin(), m_predelay[channel].end(), 0.0f);
    }

    m_predelayIndex = 0;

    m_feedback.setImmediate (m_feedback.target());
    m_damping.setImmediate (m_damping.target());
    m_mix.setImmediate (m_mix.target());
    m_width.setImmediate (m_width.target());
}

void ReverbModule::setParameter (int parameterIndex, float value) noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return;

    const auto& domain = manifest().parameters[static_cast<size_t> (parameterIndex)];
    const float clamped = domain.clampValue (value);

    if (clamped == m_values[parameterIndex])
        return;

    m_values[parameterIndex] = clamped;
    recomputeTargets();
}

float ReverbModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

void ReverbModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_predelayLength <= 0)
        return;

    if (m_targetsDirty)
        recomputeTargets();

    const bool stereo = numChannels > 1;
    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    // The predelay is a whole-sample read: a reverb tail starts a little later,
    // it does not glide, so interpolation would only cost a fractional read.
    const int predelaySamples = std::max (
        0,
        std::min (static_cast<int> (m_values[PredelayMs] * 0.001f * m_sampleRate), m_predelayLength - 1));

    for (int i = 0; i < numSamples; ++i)
    {
        const float feedback = m_feedback.next();
        const float damping = m_damping.next();
        const float mix = m_mix.next();
        const float width = m_width.next();

        const float dryLeft = left[i];
        const float dryRight = right[i];

        const int readIndex = (m_predelayIndex - predelaySamples + m_predelayLength) % m_predelayLength;
        const float inLeft = m_predelay[0][static_cast<size_t> (readIndex)];
        const float inRight = m_predelay[1][static_cast<size_t> (readIndex)];

        m_predelay[0][static_cast<size_t> (m_predelayIndex)] = flushDenormal (dryLeft);
        m_predelay[1][static_cast<size_t> (m_predelayIndex)] = flushDenormal (dryRight);

        float wet[2] { 0.0f, 0.0f };
        const float input[2] { (inLeft + inRight) * 0.5f, (inLeft + inRight) * 0.5f };

        for (int channel = 0; channel < 2; ++channel)
        {
            float sum = 0.0f;

            for (auto& comb : m_combs[channel])
                sum += comb.process (input[channel], feedback, damping);

            // Eight combs in parallel sum to eight times the input. Without this
            // normalisation a fully-wet reverb is +18 dB, which is how a "space"
            // control ends up sounding like a volume control - and how an extreme
            // case ran past the factory's bounded-output contract.
            sum *= 1.0f / static_cast<float> (kCombsPerChannel);

            for (auto& allpass : m_allpasses[channel])
                sum = allpass.process (sum);

            wet[channel] = sum;
        }

        // Width is mid/side on the wet signal only, so collapsing it never
        // touches the dry path.
        const float mid = (wet[0] + wet[1]) * 0.5f;
        const float side = (wet[0] - wet[1]) * 0.5f * width;
        const float wetLeft = mid + side;
        const float wetRight = mid - side;

        left[i] = dryLeft * (1.0f - mix) + wetLeft * mix;
        right[i] = dryRight * (1.0f - mix) + wetRight * mix;

        if (++m_predelayIndex >= m_predelayLength)
            m_predelayIndex = 0;
    }
}

} // namespace zfactory
