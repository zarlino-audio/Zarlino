#include "DelayModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
/** Longest delay the module will allocate. Two seconds covers every musical
    echo; anything longer is a looper, not a delay. */
constexpr float kMaxDelayMs = 2000.0f;

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.delay.stereo";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Stereo Delay";
    manifest.description = "Tempo-friendly stereo delay with a damped feedback loop and ping-pong routing";
    manifest.category = ModuleCategory::Mixing;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 1.2f;

    {
        ParameterManifest parameter;
        parameter.id = "timeMs";
        parameter.displayName = "Time";
        parameter.unit = "ms";
        parameter.minValue = 1.0f;
        parameter.maxValue = kMaxDelayMs;
        parameter.defaultValue = 350.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 60.0f;
        parameter.displayGroup = "Delay";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "feedback";
        parameter.displayName = "Feedback";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 0.95f;
        parameter.defaultValue = 0.3f;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Delay";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "dampHz";
        parameter.displayName = "Damping";
        parameter.unit = "Hz";
        parameter.minValue = 400.0f;
        parameter.maxValue = 18000.0f;
        parameter.defaultValue = 7000.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 40.0f;
        parameter.displayGroup = "Delay";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "pingPong";
        parameter.displayName = "Ping-Pong";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 2;
        parameter.displayGroup = "Delay";
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

float onePoleCoefficient (float frequencyHz, float sampleRate) noexcept
{
    const float bounded = std::min (frequencyHz, sampleRate * 0.45f);
    return 1.0f - std::exp (-2.0f * 3.14159265358979f * bounded / sampleRate);
}
} // namespace

//==============================================================================
DelayModule::DelayModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& DelayModule::getManifest() const noexcept
{
    return manifest();
}

void DelayModule::prepare (const PrepareContext& context)
{
    m_sampleRate = static_cast<float> (context.sampleRate);

    // +2 samples: the interpolator reads index-1 and index, and the largest
    // delay must still leave both inside the line.
    m_length = static_cast<int> (std::ceil (kMaxDelayMs * 0.001f * m_sampleRate)) + 2;

    for (auto& line : m_lines)
    {
        line.assign (static_cast<size_t> (m_length), 0.0f);
    }

    m_writeIndex = 0;
    recomputeTargets();

    m_delaySamples.prepare (context.sampleRate, 0.06, m_delaySamples.target());
    m_feedback.prepare (context.sampleRate, 0.04, m_feedback.target());
    m_mix.prepare (context.sampleRate, 0.03, m_mix.target());
    m_dampCoefficient.prepare (context.sampleRate, 0.04, m_dampCoefficient.target());

    reset();
}

void DelayModule::recomputeTargets() noexcept
{
    const float ms = std::min (m_values[TimeMs], kMaxDelayMs);
    const float samples = std::max (1.0f, std::min (ms * 0.001f * m_sampleRate, static_cast<float> (m_length - 3)));

    m_delaySamples.setTarget (samples);
    m_feedback.setTarget (m_values[Feedback]);
    m_mix.setTarget (m_values[Mix]);
    m_dampCoefficient.setTarget (onePoleCoefficient (m_values[DampHz], m_sampleRate));
    m_targetsDirty = false;
}

void DelayModule::reset() noexcept
{
    recomputeTargets();

    for (auto& line : m_lines)
        std::fill (line.begin(), line.end(), 0.0f);

    m_writeIndex = 0;
    m_damping[0] = 0.0f;
    m_damping[1] = 0.0f;

    m_delaySamples.setImmediate (m_delaySamples.target());
    m_feedback.setImmediate (m_feedback.target());
    m_mix.setImmediate (m_mix.target());
    m_dampCoefficient.setImmediate (m_dampCoefficient.target());
}

void DelayModule::setParameter (int parameterIndex, float value) noexcept
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

float DelayModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

float DelayModule::read (int channel, float delaySamples) const noexcept
{
    const auto& line = m_lines[channel];
    const float readPosition = static_cast<float> (m_writeIndex) - delaySamples;

    float position = readPosition;

    while (position < 0.0f)
        position += static_cast<float> (m_length);

    const int i0 = static_cast<int> (position);
    const float fraction = position - static_cast<float> (i0);
    const int i1 = (i0 + 1) % m_length;

    const float a = line[static_cast<size_t> (i0 % m_length)];
    const float b = line[static_cast<size_t> (i1)];

    return a + (b - a) * fraction;
}

void DelayModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    sanitiseBlock (channels, numChannels, numSamples);

    if (m_length <= 0)
        return;

    if (m_targetsDirty)
        recomputeTargets();

    const bool pingPong = m_values[PingPong] >= 0.5f;
    const bool stereo = numChannels > 1;

    float* left = channels[0];
    float* right = stereo ? channels[1] : channels[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const float delay = m_delaySamples.next();
        const float feedback = m_feedback.next();
        const float damp = m_dampCoefficient.next();
        const float mix = m_mix.next();

        const float dryLeft = left[i];
        const float dryRight = right[i];

        const float wetLeft = read (0, delay);
        const float wetRight = read (stereo ? 1 : 0, delay);

        if (pingPong)
        {
            // One echo bounces left -> right -> left. The right line carries no
            // direct input, which is what makes the pattern alternate.
            const float mono = (dryLeft + dryRight) * 0.5f;
            m_damping[0] += damp * (wetLeft - m_damping[0]);
            m_damping[1] += damp * (wetRight - m_damping[1]);

            m_lines[0][static_cast<size_t> (m_writeIndex)] = flushDenormal (mono + m_damping[1] * feedback);
            m_lines[1][static_cast<size_t> (m_writeIndex)] = flushDenormal (m_damping[0] * feedback);
        }
        else
        {
            m_damping[0] += damp * (wetLeft - m_damping[0]);
            m_damping[1] += damp * (wetRight - m_damping[1]);

            m_lines[0][static_cast<size_t> (m_writeIndex)] = flushDenormal (dryLeft + m_damping[0] * feedback);
            m_lines[1][static_cast<size_t> (m_writeIndex)] = flushDenormal (dryRight + m_damping[1] * feedback);
        }

        left[i] = dryLeft * (1.0f - mix) + wetLeft * mix;
        right[i] = dryRight * (1.0f - mix) + wetRight * mix;

        if (++m_writeIndex >= m_length)
            m_writeIndex = 0;
    }
}

} // namespace zfactory
