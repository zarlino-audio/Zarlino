/*
    ZTame Render Lab — offline A/B renderer.

    Renders deterministic demo material through the real ZTame engine and writes
    three aligned files per source so the before/after difference can be judged
    by ear (and by any null/measurement tool):

        <name>-A-dry.wav      the source, delayed by the plugin latency (phase-aligned)
        <name>-B-ztame.wav    processed with the current default settings
        <name>-C-removed.wav  exactly what ZTame took out (delta, monitor gain undone)
        <name>-A-tamed.wav    processed with a deliberately aggressive setting (demonstration)

    Usage: ZTame_RenderLab [output-directory]
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <cmath>
#include <iostream>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    //==========================================================================
    // Demo material — narrow-band resonances riding on believable source beds.
    //==========================================================================
    juce::AudioBuffer<float> makeVocalLike (int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (7);

        // four "syllables"
        const int syllable = numSamples / 4;

        for (int s = 0; s < numSamples; ++s)
        {
            const double t = (double) s / kSampleRate;
            const int syl = juce::jmin (3, s / syllable);
            const double sylT = (double) (s % syllable) / kSampleRate;

            // amplitude envelope with a short attack and a gap between syllables
            double env = std::exp (-sylT * 3.0) * (1.0 - std::exp (-sylT * 60.0));
            if (sylT > 0.45) env *= juce::jmax (0.0, 1.0 - (sylT - 0.45) * 12.0);

            // glottal source, with pitch drift per syllable
            const double f0 = 175.0 + 25.0 * syl + 4.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t);
            double voice = 0.0;
            for (int h = 1; h <= 14; ++h)
                voice += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t)
                       / (double) (h * h);

            // a narrow, ringy formant resonance — exactly the kind of thing ZTame targets
            const double ring = 0.35 * std::sin (2.0 * juce::MathConstants<double>::pi * 3150.0 * t)
                              * std::exp (-sylT * 1.5);

            const double breath = 0.01 * (rng.nextDouble() * 2.0 - 1.0);
            const auto v = (float) (env * (0.35 * voice + ring) + breath);

            b.setSample (0, s, v);
            b.setSample (1, s, v * 0.94f);
        }

        return b;
    }

    juce::AudioBuffer<float> makeDrumBus (int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (11);

        const int beat = (int) (kSampleRate * 0.5);   // 120 BPM

        for (int s = 0; s < numSamples; ++s)
        {
            const int inBeat = s % beat;
            const double t = (double) s / kSampleRate;
            const double bt = (double) inBeat / kSampleRate;

            // kick on the beat
            double kick = 0.0;
            if (bt < 0.35)
                kick = 0.9 * std::sin (2.0 * juce::MathConstants<double>::pi
                                       * (55.0 + 60.0 * std::exp (-bt * 25.0)) * bt)
                     * std::exp (-bt * 9.0);

            // snare on the off-beat
            double snare = 0.0;
            if (inBeat > beat / 2 && (inBeat - beat / 2) < (int) (kSampleRate * 0.2))
            {
                const double st = (double) (inBeat - beat / 2) / kSampleRate;
                snare = (0.35 * (rng.nextDouble() * 2.0 - 1.0) + 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 190.0 * st))
                      * std::exp (-st * 22.0);
            }

            // hats
            double hat = 0.0;
            {
                const int hp = (int) (kSampleRate * 0.125);
                const int inHp = s % hp;
                const double ht = (double) inHp / kSampleRate;
                hat = 0.12 * (rng.nextDouble() * 2.0 - 1.0) * std::exp (-ht * 90.0);
            }

            // a narrow 420 Hz "boomy tom" ring — the resonance to tame
            const double ring = 0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * 420.0 * t)
                              * (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.35 * t));

            const auto v = (float) (kick + snare + hat + ring);
            b.setSample (0, s, v);
            b.setSample (1, s, v * 0.97f);
        }

        return b;
    }

    juce::AudioBuffer<float> makeMixBed (int numSamples)
    {
        juce::AudioBuffer<float> b (2, numSamples);
        juce::Random rng (23);

        for (int s = 0; s < numSamples; ++s)
        {
            const double t = (double) s / kSampleRate;

            const double bass  = 0.30 * std::sin (2.0 * juce::MathConstants<double>::pi * 60.0 * t);
            const double chord = 0.16 * (std::sin (2.0 * juce::MathConstants<double>::pi * 262.0 * t)
                                       + std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t)
                                       + std::sin (2.0 * juce::MathConstants<double>::pi * 392.0 * t));
            // fizzy lead with a hard 2.4 kHz ring + a 6.3 kHz sizzle
            const double lead  = 0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * 2400.0 * t)
                               + 0.10 * std::sin (2.0 * juce::MathConstants<double>::pi * 6300.0 * t);
            const double floorNoise = 0.006 * (rng.nextDouble() * 2.0 - 1.0);

            const auto v = (float) (bass + chord + lead + floorNoise);
            b.setSample (0, s, v);
            b.setSample (1, s, v);
        }

        return b;
    }

    //==========================================================================
    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        file.deleteFile();   // FileOutputStream appends — always start clean

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (new juce::FileOutputStream (file), sampleRate,
                                    (unsigned int) buffer.getNumChannels(), 24, {}, 0));

        if (writer == nullptr)
        {
            std::cerr << "  !! failed to create " << file.getFullPathName() << std::endl;
            return;
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    juce::AudioBuffer<float> renderWith (ZTameAudioProcessor& proc, const juce::AudioBuffer<float>& input)
    {
        juce::AudioBuffer<float> out (input.getNumChannels(), input.getNumSamples());
        out.makeCopyOf (input);

        juce::MidiBuffer midi;
        for (int offset = 0; offset < out.getNumSamples(); offset += kBlockSize)
        {
            const int n = juce::jmin (kBlockSize, out.getNumSamples() - offset);
            juce::AudioBuffer<float> block (out.getNumChannels(), n);

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                block.copyFrom (ch, 0, out, ch, offset, n);

            proc.processBlock (block, midi);

            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.copyFrom (ch, offset, block, ch, 0, n);
        }

        return out;
    }

    void setParam (ZTameAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getValueTreeState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** A demo preset: enough range to be obvious, still musical. */
    void applyShowcasePreset (ZTameAudioProcessor& p)
    {
        setParam (p, "bypass",     0.0f);
        setParam (p, "sensitivity", 72.0f);
        setParam (p, "selectivity", 55.0f);
        setParam (p, "range",       16.0f);
        setParam (p, "attack",      6.0f);
        setParam (p, "release",     160.0f);
        setParam (p, "dryWet",      100.0f);
        setParam (p, "listenMode",  0.0f);
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::File outDir = (argc > 1)
        ? juce::File (juce::String (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ztame-renderlab");

    outDir.createDirectory();

    constexpr int numSamples = (int) (kSampleRate * 6.0);

    struct Source { const char* name; juce::AudioBuffer<float> buffer; };

    std::vector<Source> sources;
    sources.push_back ({ "vocal-ring",   makeVocalLike (numSamples) });
    sources.push_back ({ "drum-bus",     makeDrumBus (numSamples) });
    sources.push_back ({ "mix-bed",      makeMixBed (numSamples) });

    std::cout << "ZTame Render Lab -> " << outDir.getFullPathName() << std::endl;

    for (auto& source : sources)
    {
        // The engine is created fresh per source so renders are deterministic.
        auto* proc = new ZTameAudioProcessor();
        proc->prepareToPlay (kSampleRate, kBlockSize);
        applyShowcasePreset (*proc);

        const int latency = proc->getLatencySamples();

        auto processed = renderWith (*proc, source.buffer);

        // Dry reference, delayed by the plugin latency so it stays phase-aligned.
        juce::AudioBuffer<float> dry (source.buffer.getNumChannels(), numSamples);
        dry.clear();
        for (int s = latency; s < numSamples; ++s)
            for (int ch = 0; ch < dry.getNumChannels(); ++ch)
                dry.setSample (ch, s, source.buffer.getSample (ch, s - latency));

        // Removed material only (monitor gain undone → honest level).
        // Re-prepare first so the detector state at the start of this pass is identical
        // to the processed pass; that keeps dry - processed - removed at the noise floor.
        setParam (*proc, "listenMode", 1.0f);
        proc->prepareToPlay (kSampleRate, kBlockSize);
        applyShowcasePreset (*proc);
        setParam (*proc, "listenMode", 1.0f);
        auto removed = renderWith (*proc, source.buffer);
        removed.applyGain (0.5f);
        setParam (*proc, "listenMode", 0.0f);

        const juce::String base (source.name);
        writeWav (outDir.getChildFile (base + "-A-dry.wav"),     dry,       kSampleRate);
        writeWav (outDir.getChildFile (base + "-B-ztame.wav"),   processed, kSampleRate);
        writeWav (outDir.getChildFile (base + "-C-removed.wav"), removed,   kSampleRate);

        std::cout << "  " << base
                  << "  dry -6 dB point " << juce::String (dry.getRMSLevel (0, 0, numSamples), 4)
                  << "  processed " << juce::String (processed.getRMSLevel (0, 0, numSamples), 4)
                  << "  removed " << juce::String (removed.getRMSLevel (0, 0, numSamples), 4)
                  << "  (latency " << latency << " samples)"
                  << std::endl;

        // Deliberately aggressive settings — the "is it obviously doing something?" check.
        setParam (*proc, "sensitivity", 90.0f);
        setParam (*proc, "selectivity", 35.0f);
        setParam (*proc, "range",       24.0f);
        proc->prepareToPlay (kSampleRate, kBlockSize);
        applyShowcasePreset (*proc);
        setParam (*proc, "sensitivity", 90.0f);
        setParam (*proc, "selectivity", 35.0f);
        setParam (*proc, "range",       24.0f);
        auto tamed = renderWith (*proc, source.buffer);
        writeWav (outDir.getChildFile (base + "-D-max.wav"), tamed, kSampleRate);

        std::cout << "  " << base << "  max settings RMS "
                  << juce::String (tamed.getRMSLevel (0, 0, numSamples), 4) << std::endl;
    }

    std::cout << "Done." << std::endl;

    std::cout.flush();
    std::exit (0);   // audio processors are intentionally leaked (console teardown can hang)
}
