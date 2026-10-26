#include "SynthModule.h"

#include <algorithm>
#include <cmath>

namespace zfactory
{

namespace
{
// Output scale for one voice. Eight voices at different phases sum close to
// sqrt(8) times one voice, so this keeps a full chord inside a sensible headroom
// window while a single note still has a usable level.
constexpr float kVoiceScale = 0.40f;

// Envelope floor below which a releasing voice is considered silent and its
// voice is returned to the pool.
constexpr float kSilenceFloor = 1.0e-4f;

// 60 dB expressed as a natural logarithm, used to turn a "60 dB time" into a
// one-pole coefficient: exp(-ln(1000) / samples).
constexpr float kLn60Db = 6.907755278982137f;

float frequencyForNote (int midiNote, int octave, float fineCents) noexcept
{
    const double semitones = static_cast<double> (midiNote)
                                 + 12.0 * static_cast<double> (octave)
                                 + static_cast<double> (fineCents) / 100.0
                                 - 69.0;

    return static_cast<float> (440.0 * std::pow (2.0, semitones / 12.0));
}

/** polyBLEP residual: the correction that removes the step discontinuity of a
    naive saw/pulse at the point where it wraps. */
float polyBlep (double phase, double increment) noexcept
{
    if (increment <= 0.0)
        return 0.0f;

    if (phase < increment)
    {
        const double t = phase / increment;
        return static_cast<float> (t + t - t * t - 1.0);
    }

    if (phase > 1.0 - increment)
    {
        const double t = (phase - 1.0) / increment;
        return static_cast<float> (t * t + t + t + 1.0);
    }

    return 0.0f;
}

ModuleManifest makeManifest()
{
    ModuleManifest manifest;
    manifest.id = "zarlino.source.synth";
    manifest.version = { 1, 0, 0 };
    manifest.displayName = "Synth";
    manifest.description = "Eight-voice polyphonic oscillator with an amplitude envelope";
    manifest.category = ModuleCategory::Source;
    manifest.certification = CertificationState::Certified;
    manifest.supportedChannels = { ChannelMode::Mono, ChannelMode::Stereo };
    manifest.referenceLatencySamples = 0;
    manifest.cpuCostPerSample = 6.0f;
    manifest.isSource = true;

    {
        ParameterManifest parameter;
        parameter.id = "wave";
        parameter.displayName = "Waveform";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 3.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 4;
        parameter.displayGroup = "Voice";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "octave";
        parameter.displayName = "Octave";
        parameter.unit = "";
        parameter.minValue = -2.0f;
        parameter.maxValue = 2.0f;
        parameter.defaultValue = 0.0f;
        parameter.stepped = true;
        parameter.stepCount = 5;
        parameter.displayGroup = "Voice";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "fineCents";
        parameter.displayName = "Fine";
        parameter.unit = "";
        parameter.minValue = -100.0f;
        parameter.maxValue = 100.0f;
        parameter.defaultValue = 0.0f;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Voice";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "pulseWidth";
        parameter.displayName = "Width";
        parameter.unit = "";
        parameter.minValue = 0.05f;
        parameter.maxValue = 0.95f;
        parameter.defaultValue = 0.5f;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Voice";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "attackMs";
        parameter.displayName = "Attack";
        parameter.unit = "ms";
        parameter.minValue = 0.5f;
        parameter.maxValue = 4000.0f;
        parameter.defaultValue = 5.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Envelope";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "decayMs";
        parameter.displayName = "Decay";
        parameter.unit = "ms";
        parameter.minValue = 5.0f;
        parameter.maxValue = 4000.0f;
        parameter.defaultValue = 250.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Envelope";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "sustain";
        parameter.displayName = "Sustain";
        parameter.unit = "";
        parameter.minValue = 0.0f;
        parameter.maxValue = 1.0f;
        parameter.defaultValue = 0.75f;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Envelope";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "releaseMs";
        parameter.displayName = "Release";
        parameter.unit = "ms";
        parameter.minValue = 5.0f;
        parameter.maxValue = 8000.0f;
        parameter.defaultValue = 220.0f;
        parameter.logarithmic = true;
        parameter.smoothingMs = 20.0f;
        parameter.displayGroup = "Envelope";
        manifest.parameters.push_back (parameter);
    }

    {
        ParameterManifest parameter;
        parameter.id = "levelDb";
        parameter.displayName = "Level";
        parameter.unit = "dB";
        parameter.minValue = -48.0f;
        parameter.maxValue = 6.0f;
        parameter.defaultValue = -6.0f;
        parameter.smoothingMs = 20.0f;
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
SynthModule::SynthModule()
{
    for (int i = 0; i < NumParameters; ++i)
        m_values[i] = manifest().parameters[static_cast<size_t> (i)].defaultValue;
}

const ModuleManifest& SynthModule::getManifest() const noexcept
{
    return manifest();
}

void SynthModule::prepare (const PrepareContext& context)
{
    m_sampleRate = context.sampleRate > 0.0 ? context.sampleRate : 48000.0;

    // One-pole DC blocker at ~10 Hz: a narrow pulse carries real DC, and a
    // constant offset would be counted against the plugin's headroom.
    m_dcCoeff = 1.0f - static_cast<float> (2.0 * 3.14159265358979323846 * 10.0 / m_sampleRate);
    m_dcCoeff = std::min (0.9999f, std::max (0.0f, m_dcCoeff));

    recomputeTargets();

    reset();
}

void SynthModule::recomputeTargets() noexcept
{
    const auto perSample = [this] (float milliseconds) noexcept
    {
        const double seconds = std::max (1.0e-4, static_cast<double> (milliseconds) * 0.001);
        return static_cast<float> (seconds * m_sampleRate);
    };

    m_attackStep = 1.0f / std::max (1.0f, perSample (m_values[AttackMs]));
    m_decayCoeff = std::exp (-kLn60Db / std::max (1.0f, perSample (m_values[DecayMs])));
    m_releaseCoeff = std::exp (-kLn60Db / std::max (1.0f, perSample (m_values[ReleaseMs])));
    m_sustain = clamp01 (m_values[Sustain]);
    m_levelGain = std::pow (10.0f, m_values[LevelDb] / 20.0f);

    updateVoiceFrequencies();
    m_targetsDirty = false;
}

void SynthModule::updateVoiceFrequencies() noexcept
{
    const int octave = static_cast<int> (std::lround (m_values[Octave]));

    for (auto& voice : m_voices)
    {
        if (voice.note < 0)
            continue;

        const float frequency = frequencyForNote (voice.note, octave, m_values[FineCents]);
        voice.increment = std::min (0.45, static_cast<double> (frequency) / m_sampleRate);
    }
}

void SynthModule::reset() noexcept
{
    for (auto& voice : m_voices)
        voice = Voice();

    m_dcX1 = 0.0f;
    m_dcY1 = 0.0f;
    m_noteCounter = 0;

    recomputeTargets();
}

void SynthModule::setParameter (int parameterIndex, float value) noexcept
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

float SynthModule::getParameter (int parameterIndex) const noexcept
{
    if (parameterIndex < 0 || parameterIndex >= NumParameters)
        return 0.0f;

    return m_values[parameterIndex];
}

int SynthModule::numActiveVoices() const noexcept
{
    int count = 0;

    for (const auto& voice : m_voices)
        if (voice.active())
            ++count;

    return count;
}

//==============================================================================
void SynthModule::handleNoteOn (int midiNote, float velocity) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return;

    Voice* target = nullptr;

    // Retrigger the same note rather than stacking identical voices.
    for (auto& voice : m_voices)
        if (voice.active() && voice.note == midiNote)
            target = &voice;

    if (target == nullptr)
        for (auto& voice : m_voices)
            if (! voice.active())
            {
                target = &voice;
                break;
            }

    if (target == nullptr)
    {
        // Steal the oldest voice: the one that started first.
        target = &m_voices[0];

        for (auto& voice : m_voices)
            if (voice.startedAt < target->startedAt)
                target = &voice;
    }

    const float clampedVelocity = std::min (1.0f, std::max (0.0f, velocity));

    target->note = midiNote;
    target->phase = 0.0;
    target->velocityGain = 0.2f + 0.8f * clampedVelocity;
    target->envelope = 0.0f;
    target->stage = EnvelopeStage::Attack;
    target->startedAt = ++m_noteCounter;

    const int octave = static_cast<int> (std::lround (m_values[Octave]));
    const float frequency = frequencyForNote (midiNote, octave, m_values[FineCents]);
    target->increment = std::min (0.45, static_cast<double> (frequency) / m_sampleRate);
}

void SynthModule::handleNoteOff (int midiNote) noexcept
{
    for (auto& voice : m_voices)
        if (voice.active() && voice.note == midiNote)
            voice.stage = EnvelopeStage::Release;
}

void SynthModule::handleAllNotesOff() noexcept
{
    for (auto& voice : m_voices)
        if (voice.active())
            voice.stage = EnvelopeStage::Release;
}

//==============================================================================
float SynthModule::oscillatorSample (double phase, double increment, int wave,
                                     float pulseWidth) const noexcept
{
    switch (wave)
    {
        case 1: // pulse
        {
            const double width = static_cast<double> (pulseWidth);
            float value = phase < width ? 1.0f : -1.0f;
            value += polyBlep (phase, increment);

            double shifted = phase - width;

            if (shifted < 0.0)
                shifted += 1.0;

            value -= polyBlep (shifted, increment);
            return value;
        }

        case 2: // triangle
            return static_cast<float> (4.0 * std::abs (phase - 0.5) - 1.0);

        case 3: // sine
            return static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * phase));

        default: // saw
        {
            float value = static_cast<float> (2.0 * phase - 1.0);
            value -= polyBlep (phase, increment);
            return value;
        }
    }
}

void SynthModule::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    float* first = channels[0];

    if (first == nullptr)
        return;

    if (m_targetsDirty)
        recomputeTargets();

    const int wave = static_cast<int> (std::lround (m_values[Wave]));
    const float pulseWidth = m_values[PulseWidth];

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (auto& voice : m_voices)
        {
            switch (voice.stage)
            {
                case EnvelopeStage::Idle:
                    continue;

                case EnvelopeStage::Attack:
                    voice.envelope += m_attackStep;

                    if (voice.envelope >= 1.0f)
                    {
                        voice.envelope = 1.0f;
                        voice.stage = EnvelopeStage::Decay;
                    }
                    break;

                case EnvelopeStage::Decay:
                    voice.envelope = m_sustain + (voice.envelope - m_sustain) * m_decayCoeff;

                    if (voice.envelope - m_sustain < kSilenceFloor)
                    {
                        voice.envelope = m_sustain;
                        voice.stage = EnvelopeStage::Sustain;

                        // A zero sustain means the note has already ended.
                        if (m_sustain < kSilenceFloor)
                            voice.stage = EnvelopeStage::Idle;
                    }
                    break;

                case EnvelopeStage::Sustain:
                    voice.envelope = m_sustain;

                    if (m_sustain < kSilenceFloor)
                        voice.stage = EnvelopeStage::Idle;
                    break;

                case EnvelopeStage::Release:
                    voice.envelope *= m_releaseCoeff;

                    if (voice.envelope < kSilenceFloor)
                    {
                        voice = Voice();
                        continue;
                    }
                    break;
            }

            if (! voice.active())
                continue;

            sum += oscillatorSample (voice.phase, voice.increment, wave, pulseWidth)
                   * voice.envelope * voice.velocityGain;

            voice.phase += voice.increment;

            if (voice.phase >= 1.0)
                voice.phase -= 1.0;
        }

        // DC blocker then level, so a narrow pulse does not offset the chain.
        const float blocked = sum - m_dcX1 + m_dcCoeff * m_dcY1;
        m_dcX1 = flushDenormal (sum);
        m_dcY1 = flushDenormal (blocked);

        // The final tanh is a headroom guard for eight voices summing at once;
        // at any single-note level it is transparent to well under 0.1 dB, and it
        // guarantees the source can never hand the chain a value above 0 dBFS.
        first[i] = std::tanh (blocked * kVoiceScale * m_levelGain);
    }

    // A source is the same signal in every channel: an instrument's width comes
    // from the processors after it, not from the source.
    for (int channel = 1; channel < numChannels; ++channel)
        if (channels[channel] != nullptr)
            std::copy (first, first + numSamples, channels[channel]);
}

} // namespace zfactory
