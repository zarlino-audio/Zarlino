#pragma once

#include "ModuleSupport.h"

#include <cstdint>

namespace zfactory
{

//==============================================================================
/** zarlino.source.synth - an eight-voice polyphonic oscillator with a per-voice
    amplitude envelope. The only CERTIFIED module for which `isSource` is true.

    Musical role: the sound source of an instrument product. Everything else in
    the library is a processor, so an instrument spec is this module followed by
    ordinary processors - filter, saturation, chorus, delay, reverb, limiter.
    That keeps the graph model unchanged: a serial chain, no dynamic routing and
    no per-voice modulation matrix.

    VOICE ALLOCATION
      * a note that is already sounding retriggers its own voice,
      * otherwise a free voice is used,
      * otherwise the oldest voice is stolen.
    Fixed at eight voices in a fixed array: allocation, locking and note-count
    dependent work are all impossible, so process() stays realtime safe.

    ANTI-ALIASING
      Saw and pulse are generated with polyBLEP correction, so a high note does
      not fold a wall of aliases into the audible band. Triangle and sine are
      generated directly (a triangle's harmonics fall away as 1/n^2, which is
      below the aliasing floor for the musical range).

    ENVELOPE
      Linear attack, exponential decay to sustain, exponential release. Attack,
      decay and release times are 60 dB times, so "release 220 ms" means the tail
      is inaudible 220 ms after note-off.

    NOTE EVENTS
      handleNoteOn / handleNoteOff / handleAllNotesOff are realtime safe and are
      the only way sound starts: a block with no note held is silent. */
class SynthModule final : public DspModule
{
public:
    SynthModule();

    const ModuleManifest& getManifest() const noexcept override;
    void prepare (const PrepareContext& context) override;
    void reset() noexcept override;
    void process (float* const* channels, int numChannels, int numSamples) noexcept override;
    void setParameter (int parameterIndex, float value) noexcept override;
    float getParameter (int parameterIndex) const noexcept override;
    int getLatencySamples() const noexcept override { return 0; }

    void handleNoteOn (int midiNote, float velocity) noexcept override;
    void handleNoteOff (int midiNote) noexcept override;
    void handleAllNotesOff() noexcept override;

    enum ParameterIndex
    {
        Wave = 0,
        Octave,
        FineCents,
        PulseWidth,
        AttackMs,
        DecayMs,
        Sustain,
        ReleaseMs,
        LevelDb,
        NumParameters
    };

    /** Exposed for tests: how many voices are currently producing sound. */
    int numActiveVoices() const noexcept;

    static constexpr int kMaxVoices = 8;

private:
    enum class EnvelopeStage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Voice
    {
        EnvelopeStage stage = EnvelopeStage::Idle;
        int note = -1;
        double phase = 0.0;
        double increment = 0.0;
        float velocityGain = 0.0f;
        float envelope = 0.0f;
        std::uint64_t startedAt = 0;

        bool active() const noexcept { return stage != EnvelopeStage::Idle; }
    };

    void recomputeTargets() noexcept;
    void updateVoiceFrequencies() noexcept;
    float oscillatorSample (double phase, double increment, int wave, float pulseWidth) const noexcept;

    Voice m_voices[kMaxVoices];

    float m_values[NumParameters] {};

    float m_attackStep = 0.01f;
    float m_decayCoeff = 0.999f;
    float m_releaseCoeff = 0.999f;
    float m_levelGain = 0.5f;
    float m_sustain = 0.75f;

    double m_sampleRate = 48000.0;
    std::uint64_t m_noteCounter = 0;

    float m_dcX1 = 0.0f;
    float m_dcY1 = 0.0f;
    float m_dcCoeff = 0.999f;

    bool m_targetsDirty = true;
};

} // namespace zfactory
