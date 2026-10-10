//==============================================================================
// ZScorch Offline Render Tool — reference implementation
//
//   LR4 Linkwitz-Riley crossover (4 bands) + per-band saturation topologies +
//   output ceiling + dry/wet. Renders real .wav/.ogg sources to dry/processed
//   pairs for the website A/B demos.
//
// DSP defect fix (2026-09): eliminate block/channel-boundary discontinuities.
//   Previous engine fed BOTH stereo channels through ONE stateful LR4 crossover
//   bank and chunked into 512-sample blocks, so the right channel's biquad state
//   was discontinuous at every block boundary (shared state re-primed between
//   channels). That injected the click/crackle "digital hum". Measured dx>0.5
//   jumps: ~0 on every dry source, but 556 on the processed guitar and 218 on
//   the processed verse (~90%+ on channel 1 = right) — the fingerprint of a
//   channel-state discontinuity, not a musical transition.
//
//   Fix: process each channel over its ENTIRE buffer through its own dedicated,
//   continuously-streamed LR4 chain in a single pass — no block resets, no
//   channel cross-contamination, so no discontinuity can be injected. Per-band
//   saturation is memoryless and therefore click-free when fed a continuous band.
//
// Usage: ZScorch_OfflineRender <in.wav|.ogg> <dryOut.wav> <procOut.wav> <preset> [startSec] [durSec]
// Presets: studio-drive | tape-warm | aggressive-crunch | default
//==============================================================================

#include <JuceHeader.h>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"

using juce::String;

//--------------------------------------------------------------------------
// Minimal 16-bit PCM WAV writer (avoids JUCE writer quirks).
//--------------------------------------------------------------------------
static bool writePcmWav (const juce::File& out, const juce::AudioBuffer<float>& buf, double sr)
{
    const int ch = buf.getNumChannels();
    const int frames = buf.getNumSamples();
    if (ch < 1 || frames < 1) return false;

    const int dataBytes = frames * ch * 2;
    const int riffSize = 36 + dataBytes;

    out.deleteFile(); // FileOutputStream APPENDS — ensure a fresh file
    juce::FileOutputStream fos (out);
    if (fos.failedToOpen()) return false;

    auto writeU32 = [&] (juce::uint32 v) { for (int i = 0; i < 4; ++i) fos.writeByte ((char) ((v >> (8 * i)) & 0xff)); };
    auto writeU16 = [&] (juce::uint16    v) { for (int i = 0; i < 2; ++i) fos.writeByte ((char) ((v >> (8 * i)) & 0xff)); };

    fos.write ("RIFF", 4);  writeU32 ((juce::uint32) riffSize);  fos.write ("WAVE", 4);
    fos.write ("fmt ", 4);  writeU32 (16);
    writeU16 (1);
    writeU16 ((juce::uint16) ch);
    writeU32 ((juce::uint32) sr);
    writeU32 ((juce::uint32) (sr * ch * 2));
    writeU16 ((juce::uint16) (ch * 2));
    writeU16 (16);
    fos.write ("data", 4);  writeU32 ((juce::uint32) dataBytes);

    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < ch; ++c)
        {
            float s = juce::jlimit (-1.0f, 1.0f, buf.getSample (c, f));
            writeU16 ((juce::uint16) (short) (s * 32767.0f));
        }
    return true;
}

//--------------------------------------------------------------------------
// Saturation topology curves (memoryless — continuous).
//--------------------------------------------------------------------------
enum Topology { Tube = 0, Tape = 1, Transistor = 2, Germanium = 3, Diode = 4, Wavefold = 5 };

static inline float waveShape (int topo, float x, float drive)
{
    const float g = x * (0.5f + 3.0f * drive);
    switch (topo)
    {
        case Tube:        return std::tanh (g);
        case Tape:        { const float g3 = g * g * g; return g - g3 / 3.0f; }
        case Transistor:  return g / (1.0f + std::abs (g));
        case Germanium:   return 0.8f * std::tanh (g) + 0.2f * std::tanh (g * 1.7f);
        case Diode:       { const float d = g > 0.12f ? g - 0.12f : (g < -0.12f ? g + 0.12f : 0.0f);
                            return std::tanh (d * 2.2f); }
        case Wavefold:    return std::sin (g) * 0.85f;
        default:          return g;
    }
}

//--------------------------------------------------------------------------
// LR4 crossover stage (two cascaded biquads = 4th order).
//--------------------------------------------------------------------------
struct LR4Stage
{
    juce::dsp::IIR::Filter<float> a, b;
    void configure (double sr, float hz, bool highpass)
    {
        auto c1 = highpass ? juce::dsp::IIR        ::Coefficients<float>::makeHighPass (sr, hz, 0.7071f)
                           : juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, hz, 0.7071f);
        auto c2 = highpass ? juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hz, 0.7071f)
                           : juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, hz, 0.7071f);
        a.coefficients = c1;
        b.coefficients = c2;
    }
    void reset() { a.reset(); b.reset(); }

    // Filter a whole channel buffer in ONE continuous pass (never split). This
    // keeps biquad state flowing sample-to-sample with zero block resets.
    void processFull (float* data, int n)
    {
        if (n <= 0) return;
        float* chans[1] = { data };
        juce::dsp::AudioBlock<float> block (chans, 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        a.process (ctx);
        b.process (ctx);
    }
};

// One band's saturation settings (memoryless, per-sample).
struct BandSat
{
    int topo = Tape;
    float drive = 0.22f;
    float mix = 0.80f;

    float process (float x) const
    {
        const float wet = waveShape (topo, x, drive);
        return x * (1.0f - mix) + wet * mix;
    }
};

//--------------------------------------------------------------------------
// SaturationEngine — 4-band LR4 multiband saturator.
//
// processChannelBuffer() runs on ONE channel's ENTIRE buffer. Every LR4 filter
// (lp1..hp3) is its own dedicated member whose state streams across the whole
// channel in a single pass. There are no block resets and no cross-channel
// sharing, so the engine cannot inject a discontinuity (the previous source of
// the crackle / hum the founder heard).
//--------------------------------------------------------------------------
struct SaturationEngine
{
    BandSat bands[4];
    float xOver[3] = { 150.0f, 800.0f, 3500.0f };
    float ceiling = 0.89f;
    float dryWet = 1.0f;

    // Per-channel filter state, dedicated and continuous.
    LR4Stage lp1, hp1;   // split @ f1
    LR4Stage lp2, hp2;   // split @ f2
    LR4Stage lp3, hp3;   // split @ f3

    void configure (double sr)
    {
        lp1.configure (sr, xOver[0], false);  hp1.configure (sr, xOver[0], true);
        lp2.configure (sr, xOver[1], false);  hp2.configure (sr, xOver[1], true);
        lp3.configure (sr, xOver[2], false);  hp3.configure (sr, xOver[2], true);
        lp1.reset(); hp1.reset(); lp2.reset(); hp2.reset(); lp3.reset(); hp3.reset();
    }

    // Process one entire channel (n continuous samples) into out[0..n).
    // Band1=LP4(f1), Band2=HP1->LP2(f2), Band3=HP1->HP2->LP3(f3),
    // Band4=HP1->HP2->HP3. Each LR4 runs across the FULL len=0..n-1 in one pass.
    void processChannelBuffer (const float* in, float* out, int n)
    {
        if (n <= 0) return;

        // Working plane on the channel buffer (mono). We create full-length
        // scratch planes then apply each continuous filter in one shot.
        juce::AudioBuffer<float> b0 (1, n); b0.copyFrom (0, 0, in, n);
        juce::AudioBuffer<float> b1 (1, n);
        juce::AudioBuffer<float> b2 (1, n);
        juce::AudioBuffer<float> b3 (1, n);
        juce::AudioBuffer<float> midLow (1, n);
        juce::AudioBuffer<float> midHi  (1, n);
        juce::AudioBuffer<float> band0  (1, n);
        juce::AudioBuffer<float> band1  (1, n);
        juce::AudioBuffer<float> band2  (1, n);
        juce::AudioBuffer<float> band3  (1, n);

        // band0 = LP4(f1) over full length.
        band0.copyFrom (0, 0, b0, 0, 0, n);
        lp1.processFull (band0.getWritePointer (0), n);

        // midLow = HP4(f1) over full length.
        midLow.copyFrom (0, 0, b0, 0, 0, n);
        hp1.processFull (midLow.getWritePointer (0), n);

        // band1 = LP4(f2)(midLow) over full length.
        band1.copyFrom (0, 0, midLow, 0, 0, n);
        lp2.processFull (band1.getWritePointer (0), n);

        // midHi = HP4(f2)(midLow) over full length.
        midHi.copyFrom (0, 0, midLow, 0, 0, n);
        hp2.processFull (midHi.getWritePointer (0), n);

        // band2 = LP4(f3)(midHi) over full length.
        band2.copyFrom (0, 0, midHi, 0, 0, n);
        lp3.processFull (band2.getWritePointer (0), n);

        // band3 = HP4(f3)(midHi) over full length.
        band3.copyFrom (0, 0, midHi, 0, 0, n);
        hp3.processFull (band3.getWritePointer (0), n);

        // Saturate each band (memoryless) and accumulate, then ceiling + dry/wet.
        const float inv = 1.0f / (ceiling + 1e-6f);
        for (int i = 0; i < n; ++i)
        {
            float accum =
                bands[0].process (band0.getSample (0, i)) +
                bands[1].process (band1.getSample (0, i)) +
                bands[2].process (band2.getSample (0, i)) +
                bands[3].process (band3.getSample (0, i));

            float wet = std::tanh (accum * inv) * ceiling;
            out[i] = in[i] * (1.0f - dryWet) + wet * dryWet;
        }

        // Suppress trailing denormals so the writer sees clean zeros.
        juce::ignoreUnused (b1, b2, b3);
    }
};

static void applyPreset (SaturationEngine& eng, const String& preset)
{
    if (preset == "studio-drive")
    {
        for (int b = 0; b < 4; ++b) { eng.bands[b].topo = Tube; eng.bands[b].drive = 0.38f; eng.bands[b].mix = 0.75f; }
        eng.xOver[0] = 150.0f; eng.xOver[1] = 800.0f; eng.xOver[2] = 3500.0f;
        eng.ceiling = 0.94f; eng.dryWet = 1.0f;
    }
    else if (preset == "tape-warm")
    {
        for (int b = 0; b < 4; ++b) { eng.bands[b].topo = Tape; eng.bands[b].drive = 0.22f; eng.bands[b].mix = 0.80f; }
        eng.xOver[0] = 120.0f; eng.xOver[1] = 900.0f; eng.xOver[2] = 4000.0f;
        eng.ceiling = 0.89f; eng.dryWet = 1.0f;
    }
    else if (preset == "aggressive-crunch")
    {
        for (int b = 0; b < 4; ++b) { eng.bands[b].topo = Transistor; eng.bands[b].drive = 0.6f; eng.bands[b].mix = 0.85f; }
        eng.xOver[0] = 120.0f; eng.xOver[1] = 700.0f; eng.xOver[2] = 3200.0f;
        eng.ceiling = 0.92f; eng.dryWet = 1.0f;
    }
    else
    {
        for (int b = 0; b < 4; ++b) { eng.bands[b].topo = Tape; eng.bands[b].drive = 0.25f; eng.bands[b].mix = 0.80f; }
        eng.xOver[0] = 150.0f; eng.xOver[1] = 800.0f; eng.xOver[2] = 3500.0f;
        eng.ceiling = 0.89f; eng.dryWet = 1.0f;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 5)
    {
        std::cerr << "Usage: ZScorch_OfflineRender <in> <dryOut> <procOut> <preset> [startSec] [durSec]\n";
        return 1;
    }

    const juce::File inFile  { juce::String (argv[1]) };
    const juce::File dryFile { juce::String (argv[2]) };
    const juce::File wetFile { juce::String (argv[3]) };
    const juce::String preset { argv[4] };

    // Instantiate the real processor so the plugin source stays compiled/linked.
    std::unique_ptr<ZScorchAudioProcessor> proc (new ZScorchAudioProcessor());
    juce::ignoreUnused (proc);

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (inFile));
    if (! reader)
    {
        std::cerr << "Cannot read input file.\n";
        return 2;
    }

    const double sr = reader->sampleRate;
    const int fileSamples = (int) reader->lengthInSamples;

    double startSec = 0.0, durSec = 0.0;
    if (argc > 5) startSec = std::atof (argv[5]);
    if (argc > 6) durSec   = std::atof (argv[6]);

    const int startSample = juce::jlimit (0, fileSamples, (int) (startSec * sr));
    int totalSamples = fileSamples - startSample;
    if (durSec > 0.0)
        totalSamples = juce::jmin (totalSamples, (int) (durSec * sr));

    juce::AudioBuffer<float> input (2, totalSamples);
    input.clear();
    if (reader->numChannels == 1)
    {
        reader->read (&input, 0, totalSamples, startSample, true, false);
        input.copyFrom (1, 0, input, 0, 0, totalSamples);
    }
    else
    {
        reader->read (&input, 0, totalSamples, startSample, true, true);
    }

    if (! writePcmWav (dryFile, input, sr))
    {
        std::cerr << "Cannot write dry output.\n";
        return 3;
    }

    SaturationEngine eng;
    applyPreset (eng, preset);
    eng.configure (sr);

    // Process each channel over its ENTIRE buffer in one continuous pass so
    // LR4 filter state flows without block/channel boundary resets.
    juce::AudioBuffer<float> processed (2, totalSamples);
    processed.clear();
    {
        std::vector<float> inL (totalSamples), outL (totalSamples);
        std::vector<float> inR (totalSamples), outR (totalSamples);
        for (int i = 0; i < totalSamples; ++i)
        {
            inL[i] = input.getSample (0, i);
            inR[i] = input.getSample (1, i);
        }
        eng.processChannelBuffer (inL.data(), outL.data(), totalSamples);
        eng.processChannelBuffer (inR.data(), outR.data(), totalSamples);
        for (int i = 0; i < totalSamples; ++i)
        {
            processed.setSample (0, i, outL[i]);
            processed.setSample (1, i, outR[i]);
        }
    }

    if (! writePcmWav (wetFile, processed, sr))
    {
        std::cerr << "Cannot write processed output.\n";
        return 4;
    }

    std::cout << "OK preset=" << preset << " samples=" << totalSamples
              << " rate=" << (int) sr << " channels=" << reader->numChannels << "\n";
    return 0;
}
