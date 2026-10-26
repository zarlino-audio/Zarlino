//==============================================================================
// Instrument (source module) tests.
//
// The factory could only build processors before this slice. Supporting
// instruments is not "add an oscillator": a plugin that the host treats as a
// synth needs the engine to accept note events, the source module to replace its
// input rather than process it, and the spec/engine pair to agree that an
// instrument without a source would be silent. Every one of those is asserted
// here, because a silent instrument looks exactly like a broken plugin to a
// customer.
//==============================================================================

#include "TestSupport.h"

#include <cmath>
#include <string>
#include <vector>

namespace zfactory
{
namespace test
{

namespace
{
/** A validated instrument spec: the certified synth followed by a limiter, so
    the tests exercise the real "source then processors" topology. */
PluginSpec makeInstrumentSpec (ChannelMode channels = ChannelMode::Stereo)
{
    auto spec = makeBaseSpec (channels);
    spec.pluginId = "zarlino.test.instrument";
    spec.pluginName = "Factory Instrument Fixture";
    spec.effectType = EffectType::Instrument;

    addModule (spec, "synth", "zarlino.source.synth");
    addModule (spec, "safe", "zarlino.limiter");

    addParameter (spec, "level", "synth.levelDb", -48.0f, 6.0f, -6.0f, "dB");

    return spec;
}

/** Positive-going zero crossings, used to measure a sine's frequency. */
int countRisingZeroCrossings (const Block& block, int channel, int startSample)
{
    int crossings = 0;

    for (int i = std::max (1, startSample); i < block.numSamples(); ++i)
        if (block.channel (channel)[i - 1] <= 0.0f && block.channel (channel)[i] > 0.0f)
            ++crossings;

    return crossings;
}

float peakBetween (const Block& block, int start, int end)
{
    float peak = 0.0f;

    for (int ch = 0; ch < block.numChannels(); ++ch)
        for (int i = start; i < end && i < block.numSamples(); ++i)
            peak = std::max (peak, std::abs (block.channel (ch)[i]));

    return peak;
}
} // namespace

//==============================================================================
class InstrumentTests final : public juce::UnitTest
{
public:
    InstrumentTests() : juce::UnitTest ("Factory Instruments", "factory") {}

    void runTest() override
    {
        testSourcePolicy();
        testSilenceWithoutANote();
        testTuning();
        testEnvelopeShape();
        testPolyphonyAndVoiceStealing();
        testPulseDcIsRemoved();
        testDeterminism();
        testCommittedInstrumentExample();
        testNoteEventsAreRealtimeSafe();
    }

private:
    //--------------------------------------------------------------------------
    void testSourcePolicy()
    {
        beginTest ("source policy: only the synth is a source, and the spec must agree");

        const auto& registry = builtinRegistry();
        const auto* synth = registry.find ("zarlino.source.synth");

        expect (synth != nullptr, "the certified synth must be registered");

        if (synth != nullptr)
        {
            expect (synth->manifest.isSource, "the synth must declare itself a source");
            expect (synth->manifest.category == ModuleCategory::Source,
                    "the synth must live in the source category");
            expectEquals (static_cast<int> (synth->manifest.parameters.size()), 9);
        }

        int sources = 0;

        for (const auto& entry : registry.entries())
            if (entry.manifest.isSource)
                ++sources;

        expectEquals (sources, 1, "exactly one certified source exists in V1");

        // An instrument without a source would build and then make no sound.
        auto withoutSource = makeBaseSpec();
        withoutSource.effectType = EffectType::Instrument;
        addModule (withoutSource, "gain", "zarlino.gain");

        expectFailure (*this, GraphEngine::create (withoutSource, registry),
                       ErrorCode::UnsupportedValue,
                       "an instrument spec with no source module");

        // A processor containing a source would silently ignore its input.
        auto processorWithSource = makeBaseSpec();
        processorWithSource.effectType = EffectType::Utility;
        addModule (processorWithSource, "synth", "zarlino.source.synth");

        expectFailure (*this, GraphEngine::create (processorWithSource, registry),
                       ErrorCode::UnsupportedValue,
                       "a processor spec containing a source module");

        auto engine = GraphEngine::create (makeInstrumentSpec(), registry);
        expectSuccess (*this, engine, "the instrument fixture");

        if (engine.ok())
            expect (engine.value()->isInstrument(), "the engine must report an instrument graph");
    }

    //--------------------------------------------------------------------------
    void testSilenceWithoutANote()
    {
        beginTest ("an instrument is silent until a note arrives, and silent again after release");

        auto created = GraphEngine::create (makeInstrumentSpec(), builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        Block block (2, 4096);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (block.isSilent(), "no note held must render exact silence, not a low drone");

        engine.handleNoteOn (69, 0.9f);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (! block.isSilent(), "a held note must produce sound");

        engine.handleNoteOff (69);

        // Default release is 220 ms; two seconds is comfortably past it.
        for (int i = 0; i < 4; ++i)
        {
            block.clear();
            engine.process (block.pointers(), block.numChannels(), block.numSamples());
        }

        expect (block.peak() < 1.0e-4f, "a released note must fall back to silence");    }

    //--------------------------------------------------------------------------
    void testTuning()
    {
        beginTest ("tuning: A4 is 440 Hz and the octave switch is exact");

        auto created = GraphEngine::create (makeInstrumentSpec (ChannelMode::Mono), builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        auto setSynth = [&engine] (const char* parameterId, float value)
        {
            const auto& manifest = engine.modules()[0].manifest;
            engine.modules()[0].module->setParameter (manifest.indexOfParameter (parameterId), value);
        };

        setSynth ("wave", 3.0f);        // sine
        setSynth ("attackMs", 0.5f);    // no attack ramp in the measurement window
        setSynth ("sustain", 1.0f);
        setSynth ("decayMs", 5.0f);
        setSynth ("levelDb", 0.0f);

        const auto measure = [&] (int midiNote, int octave) -> float
        {
            setSynth ("octave", static_cast<float> (octave));
            engine.reset();
            engine.handleNoteOn (midiNote, 1.0f);

            Block block (1, 48000);
            block.clear();
            engine.process (block.pointers(), block.numChannels(), block.numSamples());
            engine.handleNoteOff (midiNote);

            // Measure from 12,000 samples in, clear of the envelope settling, and
            // convert the crossings to Hz over exactly the window counted.
            constexpr int start = 12000;
            const int crossings = countRisingZeroCrossings (block, 0, start);

            return static_cast<float> (crossings) * 48000.0f
                   / static_cast<float> (block.numSamples() - start);
        };

        const float a4 = measure (69, 0);
        expectWithinAbsoluteError (a4, 440.0f, 6.0f, "A4 (note 69) must be ~440 Hz");

        const float a5 = measure (81, 0);
        expectWithinAbsoluteError (a5, 880.0f, 10.0f, "A5 (note 81) must be ~880 Hz");

        const float a3 = measure (57, 0);
        expectWithinAbsoluteError (a3, 220.0f, 4.0f, "A3 (note 57) must be ~220 Hz");

        // The octave switch transposes by exactly twelve semitones.
        const float a4Down = measure (69, -1);
        expectWithinAbsoluteError (a4Down, 220.0f, 4.0f, "octave -1 must drop A4 an octave");
    }

    //--------------------------------------------------------------------------
    void testEnvelopeShape()
    {
        beginTest ("envelope: attack ramps up, sustain holds, release fades");

        auto created = GraphEngine::create (makeInstrumentSpec (ChannelMode::Mono), builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        auto setSynth = [&engine] (const char* parameterId, float value)
        {
            const auto& manifest = engine.modules()[0].manifest;
            engine.modules()[0].module->setParameter (manifest.indexOfParameter (parameterId), value);
        };

        setSynth ("attackMs", 200.0f);
        setSynth ("decayMs", 4000.0f);
        setSynth ("sustain", 1.0f);
        setSynth ("levelDb", 0.0f);

        engine.handleNoteOn (69, 1.0f);

        Block block (1, 9600);   // 200 ms
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        const float early = peakBetween (block, 0, 480);
        const float late = peakBetween (block, 9000, 9600);

        expect (late > early * 3.0f,
                "a 200 ms attack must be far louder at the end of the ramp than at its start");

        // Release: with a 5 ms release the tail must be gone within 100 ms.
        setSynth ("releaseMs", 5.0f);
        engine.handleNoteOff (69);

        Block tail (1, 4800);
        tail.clear();
        engine.process (tail.pointers(), tail.numChannels(), tail.numSamples());
        const float immediately = peakBetween (tail, 0, 96);

        tail.clear();
        engine.process (tail.pointers(), tail.numChannels(), tail.numSamples());

        expect (immediately > 0.0f, "the release must start from a sounding voice");
        // The DC blocker is an IIR, so its state decays rather than truncating:
        // -80 dB after 100 ms is silence a listener can hear, which is the
        // contract. A bit-exact zero would only be achievable by dropping the
        // state and clicking.
        expect (tail.peak() < 1.0e-4f, "a 5 ms release must be silent 100 ms later");

        // A zero sustain note ends by itself, which is what "percussive" means.
        setSynth ("sustain", 0.0f);
        setSynth ("decayMs", 20.0f);
        setSynth ("attackMs", 1.0f);
        engine.reset();
        engine.handleNoteOn (69, 1.0f);

        Block percussive (1, 9600);
        percussive.clear();
        engine.process (percussive.pointers(), percussive.numChannels(), percussive.numSamples());

        expect (peakBetween (percussive, 0, 480) > 0.0f, "a plucked note must start");
        expect (peakBetween (percussive, 4800, 9600) < 1.0e-3f,
                "a note with zero sustain must have decayed away");
    }

    //--------------------------------------------------------------------------
    void testPolyphonyAndVoiceStealing()
    {
        beginTest ("polyphony: eight voices, the ninth steals instead of growing");

        auto spec = makeInstrumentSpec();
        auto created = GraphEngine::create (spec, builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        Block block (2, 512);

        for (int voice = 0; voice < 8; ++voice)
        {
            engine.handleNoteOn (48 + voice * 3, 0.8f);
            block.clear();
            engine.process (block.pointers(), block.numChannels(), block.numSamples());
        }

        expect (block.allFinite(), "a full chord must stay finite");
        expect (block.peak() <= 1.0f, "the source must never exceed 0 dBFS");

        // The ninth note must not add a voice: the oldest is retriggered.
        engine.handleNoteOn (90, 0.8f);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (block.allFinite(), "voice stealing must stay finite");
        expect (block.peak() <= 1.0f, "voice stealing must stay bounded");

        engine.handleAllNotesOff();

        // The default release is 220 ms, so 512-sample blocks need ~21 of them.
        for (int i = 0; i < 40; ++i)
        {
            block.clear();
            engine.process (block.pointers(), block.numChannels(), block.numSamples());
        }

        expect (block.peak() < 1.0e-4f, "all-notes-off must release every voice");
    }

    //--------------------------------------------------------------------------
    void testPulseDcIsRemoved()
    {
        beginTest ("a narrow pulse is DC free, so it cannot offset the rest of the chain");

        auto created = GraphEngine::create (makeInstrumentSpec (ChannelMode::Mono), builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        auto& synth = engine.modules()[0];
        synth.module->setParameter (synth.manifest.indexOfParameter ("wave"), 1.0f);
        synth.module->setParameter (synth.manifest.indexOfParameter ("pulseWidth"), 0.08f);
        synth.module->setParameter (synth.manifest.indexOfParameter ("sustain"), 1.0f);

        engine.handleNoteOn (57, 1.0f);

        Block block (1, 24000);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        double sum = 0.0;

        for (int i = 4800; i < block.numSamples(); ++i)
            sum += block.channel (0)[i];

        const double mean = sum / static_cast<double> (block.numSamples() - 4800);

        expect (std::abs (mean) < 0.02, "the mean of a narrow pulse must be ~0");
        expect (block.allFinite(), "the pulse must stay finite");
    }

    //--------------------------------------------------------------------------
    void testDeterminism()
    {
        beginTest ("determinism: the same notes render bit-identical audio");

        const auto render = [] (std::vector<float>& out)
        {
            auto created = GraphEngine::create (makeInstrumentSpec(), builtinRegistry());

            if (! created.ok())
                return false;

            auto& engine = *created.value();

            if (engine.prepare (48000.0, 512).failed())
                return false;

            Block block (2, 2048);

            for (int note = 0; note < 3; ++note)
            {
                engine.handleNoteOn (60 + note * 5, 0.7f);
                block.clear();
                engine.process (block.pointers(), block.numChannels(), block.numSamples());

                for (int ch = 0; ch < block.numChannels(); ++ch)
                    out.insert (out.end(), block.channel (ch), block.channel (ch) + block.numSamples());
            }

            return true;
        };

        std::vector<float> first;
        std::vector<float> second;

        expect (render (first), "first render");
        expect (render (second), "second render");
        expectEquals (static_cast<int> (first.size()), static_cast<int> (second.size()));
        expect (first == second, "two identical note sequences must render identically");
    }

    //--------------------------------------------------------------------------
    /** The committed example document, not a programmatic fixture: the kernel gate
        renders every file in factory/specs, and this asserts the instrument one is
        a real playable product rather than merely valid JSON. */
    void testCommittedInstrumentExample()
    {
        beginTest ("the committed instrument example loads, builds and plays");

        auto parsed = PluginSpec::fromJsonFile (specPath ("instrument.zfactory.json"));
        expectSuccess (*this, parsed, "instrument.zfactory.json loads");

        if (! parsed.ok())
            return;

        const auto& spec = parsed.value();

        expect (spec.effectType == EffectType::Instrument, "the example declares effectType instrument");
        expect (! spec.dryWet.enabled, "a source product does not offer dry/wet");
        expectEquals (static_cast<int> (spec.modules.size()), 5);

        auto created = GraphEngine::create (spec, builtinRegistry());
        expectSuccess (*this, created, "the committed example builds");

        if (! created.ok())
            return;

        auto& engine = *created.value();
        expect (engine.isInstrument(), "the committed example is an instrument graph");

        if (! expectNoError (*this, engine.prepare (48000.0, 257), "prepare"))
            return;

        Block block (2, 257);

        // Idle: silent.
        block.clear();
        engine.process (block.pointers(), 2, 257);
        expect (block.isSilent(), "the example must be silent before a note arrives");

        // Held note: audible, finite, inside full scale.
        engine.handleNoteOn (60, 0.85f);
        double energy = 0.0;

        for (int i = 0; i < 40; ++i)   // ~0.21 s
        {
            block.clear();
            engine.process (block.pointers(), 2, 257);
            expect (block.allFinite(), "the example must stay finite while playing");
            energy += block.rms();
        }

        expect (energy > 0.0, "the example must make sound for a held note");
        expect (block.peak() <= 1.0f, "the example's limiter must keep it inside full scale");

        // Released: back to silence.
        engine.handleNoteOff (60);

        for (int i = 0; i < 400; ++i)
        {
            block.clear();
            engine.process (block.pointers(), 2, 257);
        }

        expect (block.peak() < 1.0e-4f, "the example must fall silent after release");
    }

    //--------------------------------------------------------------------------
    void testNoteEventsAreRealtimeSafe()
    {
        beginTest ("note events are safe before prepare and after a strange sequence");

        auto created = GraphEngine::create (makeInstrumentSpec(), builtinRegistry());
        expectSuccess (*this, created, "instrument graph");

        if (! created.ok())
            return;

        auto& engine = *created.value();

        // Before prepare the engine must ignore notes rather than crash or start
        // a voice that would then be rendered from an unprepared state.
        engine.handleNoteOn (60, 1.0f);
        engine.handleNoteOff (60);
        engine.handleAllNotesOff();

        expectNoError (*this, engine.prepare (48000.0, 512), "prepare");

        engine.handleNoteOn (-5, 1.0f);
        engine.handleNoteOn (400, 1.0f);
        engine.handleNoteOff (61);
        engine.handleAllNotesOff();

        Block block (2, 512);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (block.allFinite(), "out-of-range note events must not poison the output");
        expect (block.peak() < 1.0e-6f, "out-of-range note-ons must not have started a voice");

        // Out-of-domain velocities are legal MIDI abuse: clamp, stay finite.
        engine.handleNoteOn (60, -3.0f);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (block.allFinite(), "a negative velocity must not break the voice");
        expect (block.peak() <= 1.0f, "a negative velocity must not exceed full scale");

        engine.handleNoteOn (60, 40.0f);
        block.clear();
        engine.process (block.pointers(), block.numChannels(), block.numSamples());

        expect (block.allFinite(), "an over-range velocity must not break the voice");
        expect (block.peak() <= 1.0f, "an over-range velocity must not exceed full scale");

        // The all-notes-off controller is what a host sends on stop/panic.
        engine.handleAllNotesOff();

        for (int i = 0; i < 40; ++i)   // 40 * 512 samples > the 220 ms release
        {
            block.clear();
            engine.process (block.pointers(), block.numChannels(), block.numSamples());
        }

        expect (block.peak() < 1.0e-4f, "all-notes-off must silence the instrument");
    }
};

// Every factory suite registers itself by existing: the runner enumerates
// juce::UnitTest::getAllTests(), so a global instance is the registration.
InstrumentTests instrumentTests;

} // namespace test
} // namespace zfactory

void registerInstrumentTests() {}
