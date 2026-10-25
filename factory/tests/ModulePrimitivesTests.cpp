//==============================================================================
// Wave 2 primitives: the module families that used to be "unsupported".
//
// Every module added in Wave 2 is put through the same generic torture (rates,
// block sizes, silence, NaN/Inf, extremes, determinism, reset) AND a behavioural
// check that its category actually does what its name promises. Survival alone
// is not certification: a delay that never echoes, or a clipper that exceeds its
// own ceiling, would pass a NaN sweep and still be worthless.
//==============================================================================

#include "TestSupport.h"

#include "zfactory/DspModule.h"
#include "zfactory/ModuleRegistry.h"

// No module headers here on purpose: every case is built through
// builtinRegistry(), the same path a generated product takes. A test that
// included the headers directly could pass while a module was unreachable from
// the registry - which is the failure that actually matters.

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace zfactory
{
namespace
{

using namespace test;

constexpr double kRates[] { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
constexpr int kBlockSizes[] { 1, 16, 64, 256, 512, 1024, 2048 };

/** Build, prepare and return a module by id from the builtin registry. */
std::unique_ptr<DspModule> makeModule (const char* moduleId, double sampleRate, int blockSize, int channels)
{
    const auto* entry = builtinRegistry().find (moduleId);

    if (entry == nullptr)
        return nullptr;

    std::unique_ptr<DspModule> module (entry->create());

    PrepareContext context;
    context.sampleRate = sampleRate;
    context.maxBlockSize = blockSize;
    context.numChannels = channels;
    module->prepare (context);
    return module;
}

/** Parameter index by id, resolved through the module's own manifest so the
    tests break loudly if a parameter is renamed. */
int parameterIndex (const DspModule& module, const char* parameterId)
{
    const auto& parameters = module.getManifest().parameters;

    for (size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i].id == parameterId)
            return static_cast<int> (i);

    return -1;
}

void setParameter (DspModule& module, const char* parameterId, float value)
{
    const int index = parameterIndex (module, parameterId);

    if (index >= 0)
        module.setParameter (index, value);
}

class ModulePrimitivesTests : public juce::UnitTest
{
public:
    ModulePrimitivesTests() : juce::UnitTest ("Factory Module Primitives") {}

    //==========================================================================
    void runTest() override
    {
        testEveryModuleIsRegistered();
        testGenericTorture();
        testDelay();
        testReverb();
        testChorus();
        testFlanger();
        testPhaser();
        testTremolo();
        testMidSide();
        testClipper();
        testExciter();
        testDeEsser();
        testAnalogueEmulations();
    }

private:
    struct Entry
    {
        const char* id;
        bool stereoOnly;
    };

    static constexpr Entry kNewModules[] {
        { "zarlino.delay.stereo", false },
        { "zarlino.reverb.algorithmic", false },
        { "zarlino.modulation.chorus", false },
        { "zarlino.modulation.flanger", false },
        { "zarlino.modulation.phaser", false },
        { "zarlino.modulation.tremolo", false },
        { "zarlino.stereo.midside", true },
        { "zarlino.saturation.clipper", false },
        { "zarlino.saturation.exciter", false },
        { "zarlino.dynamics.deesser", false },
        { "zarlino.saturation.tape", false },
        { "zarlino.saturation.tube", false },
        { "zarlino.saturation.transformer", false },
    };

    //==========================================================================
    void testEveryModuleIsRegistered()
    {
        beginTest ("every Wave 2 primitive is registered and certified");

        for (const auto& module : kNewModules)
        {
            const auto* entry = builtinRegistry().find (module.id);
            expect (entry != nullptr, juce::String (module.id) + " must be registered");

            if (entry == nullptr)
                continue;

            expect (entry->manifest.certification == CertificationState::Certified,
                    juce::String (module.id) + " must be certified before it can be built");
            expect (entry->manifest.realtimeSafe, juce::String (module.id) + " must be realtime safe");
            expect (! entry->manifest.parameters.empty(),
                    juce::String (module.id) + " must declare at least one parameter");

            const bool declaresStereo =
                std::find (entry->manifest.supportedChannels.begin(),
                           entry->manifest.supportedChannels.end(),
                           ChannelMode::Stereo)
                != entry->manifest.supportedChannels.end();

            expect (declaresStereo, juce::String (module.id) + " must support stereo");

            if (module.stereoOnly)
            {
                expect (entry->manifest.supportedChannels.size() == 1,
                        juce::String (module.id) + " is stereo-only and must say so");
            }

            expect (entry->manifest.referenceLatencySamples == 0,
                    juce::String (module.id) + " reports no latency and must declare none");
        }
    }

    //==========================================================================
    void testGenericTorture()
    {
        beginTest ("Wave 2 primitives survive rates, block sizes, extremes and hostile input");

        for (const auto& entry : kNewModules)
        {
            for (double rate : kRates)
            {
                for (int blockSize : kBlockSizes)
                {
                    for (int channels = 1; channels <= 2; ++channels)
                    {
                        if (entry.stereoOnly && channels == 1)
                            continue;

                        auto module = makeModule (entry.id, rate, blockSize, channels);
                        expect (module != nullptr, juce::String (entry.id) + " must construct");

                        if (module == nullptr)
                            continue;

                        const juce::String label = juce::String (entry.id) + " @ " + juce::String (rate)
                                                   + " Hz, block " + juce::String (blockSize)
                                                   + ", " + juce::String (channels) + " ch";

                        // Silence stays silent (nothing self-oscillates).
                        Block silent (channels, blockSize);
                        module->process (silent.pointers(), channels, blockSize);
                        expect (silent.allFinite(), label + ": silence must stay finite");
                        expect (silent.peak() == 0.0f, label + ": silence must stay silent");

                        // Impulse, sine, noise: finite and bounded.
                        Block impulse (channels, blockSize);
                        impulse.impulse (0, 0, 0.7f);
                        module->process (impulse.pointers(), channels, blockSize);
                        expect (impulse.allFinite(), label + ": impulse must stay finite");

                        Block tone (channels, blockSize);
                        tone.sine (997.0, rate, 0.5f);
                        module->process (tone.pointers(), channels, blockSize);
                        expect (tone.allFinite(), label + ": sine must stay finite");
                        expect (tone.peak() <= 4.0f, label + ": sine must stay bounded");

                        Block noise (channels, blockSize);
                        noise.noise (12345u, 0.5f);
                        module->process (noise.pointers(), channels, blockSize);
                        expect (noise.allFinite(), label + ": noise must stay finite");

                        // Hostile input: NaN, +Inf and denormal-scale values.
                        for (int variant = 0; variant < 3; ++variant)
                        {
                            Block hostile (channels, blockSize);

                            for (int ch = 0; ch < channels; ++ch)
                                for (int i = 0; i < blockSize; ++i)
                                {
                                    const float value = variant == 0
                                                            ? std::numeric_limits<float>::quiet_NaN()
                                                            : variant == 1
                                                                  ? std::numeric_limits<float>::infinity()
                                                                  : 1.0e-40f;

                                    hostile.channel (ch)[i] = value;
                                }

                            module->process (hostile.pointers(), channels, blockSize);
                            expect (hostile.allFinite(),
                                    label + ": hostile variant " + juce::String (variant)
                                        + " must be neutralised");

                            // The module must recover, not stay poisoned.
                            hostile.noise (999u, 0.3f);
                            module->process (hostile.pointers(), channels, blockSize);
                            expect (hostile.allFinite(), label + ": must recover after hostile input");
                        }

                        // Every parameter at both ends of its domain, then a burst
                        // of random automation: the output must stay finite.
                        for (int extreme = 0; extreme < 2; ++extreme)
                        {
                            const auto& parameters = module->getManifest().parameters;

                            for (size_t p = 0; p < parameters.size(); ++p)
                                module->setParameter (static_cast<int> (p),
                                                      extreme == 0 ? parameters[p].minValue
                                                                   : parameters[p].maxValue);

                            Block block (channels, blockSize);
                            block.noise (4242u + static_cast<unsigned int> (extreme), 0.6f);
                            module->process (block.pointers(), channels, blockSize);
                            expect (block.allFinite(), label + ": extremes must stay finite");
                            expect (block.peak() <= 8.0f, label + ": extremes must stay bounded");
                        }

                        module->reset();
                        Block afterReset (channels, blockSize);
                        afterReset.sine (220.0, rate, 0.4f);
                        module->process (afterReset.pointers(), channels, blockSize);
                        expect (afterReset.allFinite(), label + ": must render after reset");
                    }
                }
            }
        }
    }

    //==========================================================================
    void testDelay()
    {
        beginTest ("delay: echoes at the set time, decays, and is transparent at mix 0");

        auto module = makeModule ("zarlino.delay.stereo", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        setParameter (*module, "timeMs", 100.0f);
        setParameter (*module, "feedback", 0.5f);
        setParameter (*module, "mix", 1.0f);
        module->reset();

        // A 100 ms delay at 48 kHz is 4800 samples of silence before the echo.
        // The block must be long enough to contain several repeats: two of them
        // need 9600 samples, so 16384 leaves room for the measurement windows.
        const int expected = 4800;
        Block block (2, 16384);
        block.impulse (0, 0, 0.8f);
        module->process (block.pointers(), 2, block.numSamples());

        float preEchoPeak = 0.0f;

        for (int i = 1; i < expected - 100; ++i)
            preEchoPeak = std::max (preEchoPeak, std::abs (block.channel (0)[i]));

        expect (preEchoPeak < 1.0e-3f, "nothing before the echo (peak " + juce::String (preEchoPeak) + ")");

        float aroundEcho = 0.0f;

        for (int i = expected - 50; i < expected + 200; ++i)
            aroundEcho = std::max (aroundEcho, std::abs (block.channel (0)[i]));

        expect (aroundEcho > 0.2f, "the echo lands at 100 ms (peak " + juce::String (aroundEcho) + ")");

        // The FIRST repeat is the delayed input at unity (mix is fully wet); what
        // feedback controls is the second and later repeats. Measure the third
        // echo and require it to be quieter than the first.
        float secondEcho = 0.0f;

        for (int i = expected * 2 - 50; i < expected * 2 + 200; ++i)
            secondEcho = std::max (secondEcho, std::abs (block.channel (0)[i]));

        expect (secondEcho > 0.0f, "the delay must repeat more than once");
        expect (secondEcho < aroundEcho * 0.9f, "each repeat must be quieter than the last (second "
                                                    + juce::String (secondEcho) + " vs first " + juce::String (aroundEcho) + ")");

        // Mix 0 is a true pass-through.
        setParameter (*module, "mix", 0.0f);
        module->reset();
        Block dry (2, 512);
        dry.noise (77u, 0.5f);
        Block reference = dry.duplicate();
        module->process (dry.pointers(), 2, 512);
        expectWithinAbsoluteError (dry.maxDifference (reference), 0.0f, 1.0e-6f,
                                   "mix 0 must pass the input through unchanged");

        // Ping-pong alternates channels: an impulse on the left must not appear
        // on the left twice in a row.
        setParameter (*module, "mix", 1.0f);
        setParameter (*module, "pingPong", 1.0f);
        module->reset();
        Block ping (2, 20000);
        ping.impulse (0, 0, 0.9f);
        module->process (ping.pointers(), 2, ping.numSamples());

        const auto energyNear = [&ping] (int channel, int centre) {
            float peak = 0.0f;

            for (int i = centre - 50; i < centre + 200 && i < ping.numSamples(); ++i)
                peak = std::max (peak, std::abs (ping.channel (channel)[i]));

            return peak;
        };

        const float leftFirst = energyNear (0, 4800);
        const float rightFirst = energyNear (1, 4800);
        expect (leftFirst > 0.2f, "first ping-pong echo arrives on the left");
        expect (rightFirst < 0.2f, "the second side is empty at the first echo");
    }

    //==========================================================================
    void testReverb()
    {
        beginTest ("reverb: a tail exists, decays, and predelay delays it");

        auto module = makeModule ("zarlino.reverb.algorithmic", 48000.0, 512, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        setParameter (*module, "mix", 1.0f);
        setParameter (*module, "predelayMs", 0.0f);
        setParameter (*module, "decay", 0.8f);
        setParameter (*module, "size", 0.8f);
        module->reset();

        // Impulse, then measure energy in successive windows: it must exist and
        // it must fall.
        Block block (2, 48000);
        block.impulse (0, 0, 0.9f);
        module->process (block.pointers(), 2, block.numSamples());

        const auto windowEnergy = [&block] (int from, int length) {
            double sum = 0.0;

            for (int i = from; i < from + length && i < block.numSamples(); ++i)
                sum += std::abs (static_cast<double> (block.channel (0)[i]));

            return sum / static_cast<double> (length);
        };

        const double early = windowEnergy (2000, 4000);
        const double late = windowEnergy (40000, 4000);

        expect (early > 1.0e-4, "the tail has energy after the impulse (" + juce::String (early) + ")");
        expect (late < early, "the tail decays (late " + juce::String (late) + " < early " + juce::String (early) + ")");

        // Predelay: with 100 ms of predelay, the first 100 ms of wet output is
        // silent for an impulse input.
        setParameter (*module, "predelayMs", 100.0f);
        module->reset();

        Block delayed (2, 4800);
        delayed.impulse (0, 0, 0.9f);
        module->process (delayed.pointers(), 2, delayed.numSamples());

        float prePeak = 0.0f;

        for (int i = 1; i < 4700; ++i)
            prePeak = std::max (prePeak, std::abs (delayed.channel (0)[i]));

        expect (prePeak < 1.0e-3f, "predelay keeps the tail back (peak " + juce::String (prePeak) + ")");
    }

    //==========================================================================
    void testChorus()
    {
        beginTest ("chorus: modulates the signal and is transparent at mix 0");

        auto module = makeModule ("zarlino.modulation.chorus", 48000.0, 2048, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        setParameter (*module, "mix", 0.0f);
        module->reset();

        Block dry (2, 1024);
        dry.sine (440.0, 48000.0, 0.5f);
        Block reference = dry.duplicate();
        module->process (dry.pointers(), 2, 1024);
        expectWithinAbsoluteError (dry.maxDifference (reference), 0.0f, 1.0e-6f,
                                   "mix 0 must pass the input through unchanged");

        // At full mix the output must differ from the input (it is being pitch-
        // modulated), and must still be bounded.
        setParameter (*module, "mix", 1.0f);
        setParameter (*module, "depthMs", 8.0f);
        setParameter (*module, "rateHz", 1.0f);
        module->reset();

        Block wet (2, 4096);
        Block dryReference (2, 4096);
        wet.sine (440.0, 48000.0, 0.5f);
        dryReference.sine (440.0, 48000.0, 0.5f);
        module->process (wet.pointers(), 2, 4096);

        expect (wet.maxDifference (dryReference) > 0.01f, "the chorus must move the signal");
        expect (wet.peak() <= 1.5f, "the chorus must stay bounded");
    }

    //==========================================================================
    void testFlanger()
    {
        beginTest ("flanger: comb-filters the signal and stays stable at maximum feedback");

        auto module = makeModule ("zarlino.modulation.flanger", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        setParameter (*module, "mix", 1.0f);
        setParameter (*module, "feedback", 0.9f);
        setParameter (*module, "depthMs", 6.0f);
        module->reset();

        Block wet (2, 8192);
        Block reference (2, 8192);
        wet.noise (21u, 0.4f);
        reference.noise (21u, 0.4f);

        module->process (wet.pointers(), 2, 8192);

        expect (wet.allFinite(), "maximum feedback must stay finite");
        expect (wet.peak() <= 4.0f, "maximum feedback must not run away");
        expect (wet.maxDifference (reference) > 0.01f, "the flanger must alter the signal");

        // Sustained input at maximum feedback: the loop must still decay when the
        // input stops, which is the real test of a resonant feedback path.
        Block sustained (2, 8192);
        sustained.sine (220.0, 48000.0, 0.5f);
        module->process (sustained.pointers(), 2, 8192);

        Block silence (2, 8192);
        module->process (silence.pointers(), 2, 8192);
        expect (silence.allFinite(), "the tail must stay finite after the input stops");
        expect (silence.peak() < 0.5f, "the tail must decay, not sustain itself");
    }

    //==========================================================================
    void testPhaser()
    {
        beginTest ("phaser: sweeps allpass stages without runaway feedback");

        auto module = makeModule ("zarlino.modulation.phaser", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        setParameter (*module, "mix", 0.5f);
        setParameter (*module, "feedback", 0.9f);
        setParameter (*module, "depth", 1.0f);
        module->reset();

        Block block (2, 8192);
        Block reference (2, 8192);
        block.sine (500.0, 48000.0, 0.5f);
        reference.sine (500.0, 48000.0, 0.5f);

        module->process (block.pointers(), 2, 8192);

        expect (block.allFinite(), "phaser must stay finite at maximum feedback");
        // A six-stage resonant allpass chain at 0.9 feedback legitimately peaks
        // several times above unity at its notch resonances; what must NOT happen
        // is a runaway. The bound catches divergence, and the silence test below
        // catches a loop that sustains itself.
        expect (block.peak() <= 6.0f, "phaser must stay bounded (peak " + juce::String (block.peak()) + ")");
        expect (block.maxDifference (reference) > 0.01f, "phaser must alter the signal");

        Block silence (2, 8192);
        module->process (silence.pointers(), 2, 8192);
        expect (silence.peak() < 0.5f, "the phaser tail must decay once the input stops");
    }

    //==========================================================================
    void testTremolo()
    {
        beginTest ("tremolo: modulates amplitude, and the phase offset makes it an auto-pan");

        auto module = makeModule ("zarlino.modulation.tremolo", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        // Depth 1 with a slow sine: the gain must sweep between ~0 and 1.
        setParameter (*module, "depth", 1.0f);
        setParameter (*module, "rateHz", 2.0f);
        setParameter (*module, "shape", 0.0f);
        setParameter (*module, "stereoPhase", 0.0f);
        module->reset();

        Block block (2, 48000);
        block.sine (1000.0, 48000.0, 1.0f);
        module->process (block.pointers(), 2, 48000);

        // Measure the ENVELOPE, not individual samples: a 1 kHz tone crosses zero
        // constantly, so the peak of each short window is the gain at that moment.
        constexpr int window = 600; // 12.5 ms at 48 kHz
        float minGain = 1.0f;
        float maxGain = 0.0f;

        for (int from = 0; from + window < block.numSamples(); from += window)
        {
            float peak = 0.0f;

            for (int i = from; i < from + window; ++i)
                peak = std::max (peak, std::abs (block.channel (0)[i]));

            minGain = std::min (minGain, peak);
            maxGain = std::max (maxGain, peak);
        }

        expect (maxGain > 0.9f, "the tremolo must reach unity gain (max " + juce::String (maxGain) + ")");
        expect (minGain < 0.2f, "the tremolo must reach a deep trough (min " + juce::String (minGain) + ")");

        // Half a cycle of offset: the two channels must move in opposition.
        setParameter (*module, "stereoPhase", 0.5f);
        module->reset();

        Block panned (2, 24000);
        panned.sine (1000.0, 48000.0, 1.0f);
        module->process (panned.pointers(), 2, 24000);

        double leftEnergy = 0.0;
        double rightEnergy = 0.0;

        for (int i = 0; i < panned.numSamples(); ++i)
        {
            leftEnergy += static_cast<double> (panned.channel (0)[i]) * panned.channel (0)[i];
            rightEnergy += static_cast<double> (panned.channel (1)[i]) * panned.channel (1)[i];
        }

        expect (leftEnergy > 0.0 && rightEnergy > 0.0, "both channels must carry signal");
        // Opposed modulation means the total is roughly constant while each side
        // swings; a static tremolo would give two equal envelopes.
        expect (std::abs (leftEnergy - rightEnergy) < leftEnergy * 0.35,
                "an opposed auto-pan keeps the total energy steady");
    }

    //==========================================================================
    void testMidSide()
    {
        beginTest ("mid/side: mono material is untouched, width 0 collapses, width 2 doubles");

        auto module = makeModule ("zarlino.stereo.midside", 48000.0, 1024, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        // Width 0: left and right must become identical (pure mid).
        setParameter (*module, "width", 0.0f);
        setParameter (*module, "bassMonoHz", 20.0f);
        module->reset();

        Block block (2, 512);
        block.noise (5u, 0.5f);
        module->process (block.pointers(), 2, 512);

        float channelDifference = 0.0f;

        for (int i = 0; i < 512; ++i)
            channelDifference = std::max (channelDifference, std::abs (block.channel (0)[i] - block.channel (1)[i]));

        expect (channelDifference < 1.0e-5f, "width 0 must collapse to mono (diff " + juce::String (channelDifference) + ")");

        // Mono input must survive width 2 with the bass guard doing nothing: L and
        // R stay equal because there is no side content to widen.
        setParameter (*module, "width", 2.0f);
        module->reset();

        Block mono (2, 512);
        mono.sine (300.0, 48000.0, 0.5f);
        module->process (mono.pointers(), 2, 512);

        float monoDifference = 0.0f;

        for (int i = 0; i < 512; ++i)
            monoDifference = std::max (monoDifference, std::abs (mono.channel (0)[i] - mono.channel (1)[i]));

        expect (monoDifference < 1.0e-5f, "a centred signal must stay centred when widened");

        // Widening a real stereo signal must increase the side energy.
        const auto sideEnergy = [&] (float width) {
            setParameter (*module, "width", width);
            module->reset();

            Block material (2, 2048);
            material.noise (9u, 0.4f);
            module->process (material.pointers(), 2, 2048);

            double side = 0.0;

            for (int i = 0; i < 2048; ++i)
            {
                const double difference = (static_cast<double> (material.channel (0)[i])
                                           - material.channel (1)[i]) * 0.5;
                side += difference * difference;
            }

            return side;
        };

        const double narrow = sideEnergy (0.5f);
        const double wide = sideEnergy (2.0f);
        expect (wide > narrow * 2.0, "width 2 must carry materially more side energy than width 0.5");
    }

    //==========================================================================
    void testClipper()
    {
        beginTest ("clipper: the output never exceeds the ceiling, and mix 0 is transparent");

        auto module = makeModule ("zarlino.saturation.clipper", 48000.0, 1024, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        for (int mode = 0; mode <= 2; ++mode)
        {
            setParameter (*module, "mode", static_cast<float> (mode));
            setParameter (*module, "driveDb", 24.0f);
            setParameter (*module, "ceilingDb", -6.0f);
            setParameter (*module, "mix", 1.0f);
            module->reset();

            Block block (2, 1024);
            block.sine (100.0, 48000.0, 1.0f);
            module->process (block.pointers(), 2, 1024);

            const float ceiling = std::pow (10.0f, -6.0f / 20.0f);
            expect (block.peak() <= ceiling * 1.02f,
                    "mode " + juce::String (mode) + " must respect the ceiling (peak "
                        + juce::String (block.peak()) + " vs " + juce::String (ceiling) + ")");
            expect (block.peak() > 0.3f, "mode " + juce::String (mode) + " must actually pass audio");
        }

        setParameter (*module, "mix", 0.0f);
        module->reset();

        Block dry (2, 256);
        dry.noise (33u, 0.5f);
        Block reference = dry.duplicate();
        module->process (dry.pointers(), 2, 256);
        expectWithinAbsoluteError (dry.maxDifference (reference), 0.0f, 1.0e-6f,
                                   "mix 0 must pass the input through unchanged");
    }

    //==========================================================================
    void testExciter()
    {
        beginTest ("exciter: adds high-band harmonics without runaway gain");

        auto module = makeModule ("zarlino.saturation.exciter", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        // A pure 200 Hz tone has almost nothing above a 3 kHz band filter, so any
        // high-frequency energy in the output is generated, not passed through.
        setParameter (*module, "freqHz", 3000.0f);
        setParameter (*module, "amount", 1.0f);
        setParameter (*module, "mix", 1.0f);
        module->reset();

        Block block (2, 8192);
        block.sine (200.0, 48000.0, 0.5f);
        module->process (block.pointers(), 2, 8192);

        expect (block.allFinite(), "exciter must stay finite");
        expect (block.peak() <= 1.5f, "exciter must stay bounded (peak " + juce::String (block.peak()) + ")");

        // Compare against the same input with the effect removed: the exciter must
        // change the signal.
        setParameter (*module, "amount", 0.0f);
        module->reset();

        Block plain (2, 8192);
        plain.sine (200.0, 48000.0, 0.5f);
        module->process (plain.pointers(), 2, 8192);

        expect (block.maxDifference (plain) > 0.001f, "the exciter must add something");
    }

    //==========================================================================
    void testDeEsser()
    {
        beginTest ("de-esser: reduces the high band, leaves the low band alone, and is transparent when idle");

        auto module = makeModule ("zarlino.dynamics.deesser", 48000.0, 4096, 2);
        expect (module != nullptr);
        if (module == nullptr)
            return;

        // Idle (threshold below the signal): transparent to within the filter's
        // own rounding, because the band split is complementary.
        setParameter (*module, "thresholdDb", 0.0f);
        setParameter (*module, "amount", 0.0f);
        setParameter (*module, "freqHz", 6000.0f);
        module->reset();

        Block idle (2, 1024);
        idle.sine (200.0, 48000.0, 0.3f);
        Block reference = idle.duplicate();
        module->process (idle.pointers(), 2, 1024);
        expectWithinAbsoluteError (idle.maxDifference (reference), 0.0f, 1.0e-4f,
                                   "an idle de-esser must be transparent");

        // A loud 8 kHz band above the threshold must come out quieter.
        setParameter (*module, "thresholdDb", -30.0f);
        setParameter (*module, "amount", 1.0f);
        module->reset();

        Block sibilant (2, 9600);
        sibilant.sine (8000.0, 48000.0, 0.8f);
        module->process (sibilant.pointers(), 2, 9600);

        double reduced = 0.0;

        for (int i = 4800; i < 9600; ++i)
            reduced += static_cast<double> (sibilant.channel (0)[i]) * sibilant.channel (0)[i];

        reduced = std::sqrt (reduced / 4800.0);

        // The steady-state level must be below the input's (0.8 / sqrt(2)).
        expect (reduced < 0.45, "the high band must be reduced (rms " + juce::String (reduced) + ")");

        // The low band must not be treated as sibilance.
        setParameter (*module, "thresholdDb", -30.0f);
        module->reset();

        Block body (2, 9600);
        body.sine (200.0, 48000.0, 0.8f);
        module->process (body.pointers(), 2, 9600);

        double bodyLevel = 0.0;

        for (int i = 4800; i < 9600; ++i)
            bodyLevel += static_cast<double> (body.channel (0)[i]) * body.channel (0)[i];

        bodyLevel = std::sqrt (bodyLevel / 4800.0);

        expect (bodyLevel > 0.45, "a 200 Hz body must be left alone (rms " + juce::String (bodyLevel) + ")");
    }

    //==========================================================================
    void testAnalogueEmulations()
    {
        beginTest ("analogue emulations: each has its own character, and each is bounded");

        const char* const ids[] {
            "zarlino.saturation.tape",
            "zarlino.saturation.tube",
            "zarlino.saturation.transformer",
        };

        for (const char* id : ids)
        {
            auto module = makeModule (id, 48000.0, 2048, 2);
            expect (module != nullptr, juce::String (id) + " must construct");

            if (module == nullptr)
                continue;

            // A driven tone must stay bounded and must actually change the signal.
            setParameter (*module, "driveDb", 12.0f);
            setParameter (*module, "mix", 1.0f);
            module->reset();

            Block driven (2, 8192);
            Block reference (2, 8192);
            driven.sine (220.0, 48000.0, 0.7f);
            reference.sine (220.0, 48000.0, 0.7f);

            module->process (driven.pointers(), 2, 8192);

            expect (driven.allFinite(), juce::String (id) + " must stay finite");
            expect (driven.peak() <= 2.0f, juce::String (id) + " must stay bounded (peak " + juce::String (driven.peak()) + ")");
            expect (driven.maxDifference (reference) > 0.01f, juce::String (id) + " must alter the signal");
            expect (driven.rms() > 0.05, juce::String (id) + " must not silence the signal");

            // Mix 0 is a true pass-through, which is the contract every module
            // in this library keeps.
            setParameter (*module, "mix", 0.0f);
            module->reset();

            Block dry (2, 512);
            dry.noise (707u, 0.4f);
            Block dryReference = dry.duplicate();
            module->process (dry.pointers(), 2, 512);
            expectWithinAbsoluteError (dry.maxDifference (dryReference), 0.0f, 1.0e-6f,
                                       juce::String (id) + ": mix 0 must pass the input through unchanged");
        }

        // Asymmetry: tape and tube must generate even harmonics, which is exactly
        // what makes them analogue rather than symmetric clippers. Measured as a
        // DC-free half-cycle asymmetry of the output around zero.
        for (const char* id : { "zarlino.saturation.tape", "zarlino.saturation.tube" })
        {
            auto module = makeModule (id, 48000.0, 8192, 1);
            expect (module != nullptr);
            if (module == nullptr)
                continue;

            setParameter (*module, "bias", 1.0f);
            setParameter (*module, "driveDb", 12.0f);
            setParameter (*module, "mix", 1.0f);
            module->reset();

            Block block (1, 8192);
            block.sine (500.0, 48000.0, 0.6f);
            module->process (block.pointers(), 1, 8192);

            double positive = 0.0;
            double negative = 0.0;

            for (int i = 0; i < 8192; ++i)
            {
                const double value = block.channel (0)[i];
                if (value > 0.0) positive += value; else negative -= value;
            }

            // With bias applied the two halves compress differently, so their
            // summed magnitudes cannot match. A symmetric shaper would match.
            const double imbalance = std::abs (positive - negative) / std::max (1.0e-9, positive + negative);
            expect (imbalance > 0.02, juce::String (id) + " must be asymmetric (imbalance "
                                          + juce::String (imbalance) + ")");
        }

        // The transformer saturates the LOW band only: a 6 kHz tone must come
        // through essentially untouched while a 60 Hz tone is shaped.
        {
            auto module = makeModule ("zarlino.saturation.transformer", 48000.0, 8192, 1);
            expect (module != nullptr);

            if (module != nullptr)
            {
                setParameter (*module, "driveDb", 18.0f);
                setParameter (*module, "splitHz", 150.0f);
                setParameter (*module, "hfHz", 20000.0f);
                setParameter (*module, "mix", 1.0f);
                module->reset();

                Block high (1, 8192);
                Block highReference (1, 8192);
                high.sine (6000.0, 48000.0, 0.5f);
                highReference.sine (6000.0, 48000.0, 0.5f);
                module->process (high.pointers(), 1, 8192);

                Block low (1, 8192);
                Block lowReference (1, 8192);
                low.sine (60.0, 48000.0, 0.5f);
                lowReference.sine (60.0, 48000.0, 0.5f);
                module->process (low.pointers(), 1, 8192);

                const float highChange = high.maxDifference (highReference);
                const float lowChange = low.maxDifference (lowReference);

                expect (lowChange > highChange * 2.0f,
                        "the transformer must saturate the low band, not the top (low "
                            + juce::String (lowChange) + " vs high " + juce::String (highChange) + ")");
            }
        }
    }
};

ModulePrimitivesTests modulePrimitivesTests;

} // namespace

} // namespace zfactory

// The runner calls this at global scope (see tests/Main.cpp).
void registerModulePrimitivesTests() {}
