/*
    ZTame real-instrument A/B renderer (canonical release audio pipeline).

    Renders genuine recorded sources through the real ZTame engine and writes
    per source:

        <name>-A-dry.wav       dry master - latency-aligned bypass (honest reference)
        <name>-B-wet-raw.wav   wet master - ZTame engaged, UNMODIFIED by measurement
        <name>-B-wet.wav       comparison copy - wet loudness-matched to the dry file

    Masters preserve the untouched render; only the comparison copy carries the
    level-matched transformation, which is recorded in render-metrics.json. The
    A/B is never made to sound better through a level bias.

    Also writes render-metrics.json (machine-readable): per-source peak / RMS /
    integrated LUFS for dry, raw wet and matched wet, the applied trim, latency
    and the source's native sample rate. Sources whose rate differs from 48000 Hz
    are resampled with a deterministic JUCE Lagrange interpolator before rendering
    (recorded per source as "resampled").

    Usage: ZTame_RealRender <manifest.json>

    Manifest JSON:
        {
          "outputDir": "...",
          "version": "1.2.0",
          "gitHead": "...",
          "sources": [ { "name": "...", "path": "...", "startSec": 0, "seconds": 12 } ]
        }

    Note: deliberately NO json library. The manifest is a flat document produced by
    this repo's tooling, so a small purpose-built reader keeps the target free of
    extra dependencies.
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <limits>
namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    //==========================================================================
    // Manifest types + a small purpose-built reader.
    //==========================================================================
    struct ManifestSource
    {
        juce::String name, path;
        double startSec = 0.0;
        double seconds  = 12.0;
    };

    struct Manifest
    {
        juce::String outputDir, version, gitHead;
        std::vector<ManifestSource> sources;
    };

    juce::String jsonString (const juce::String& json, const juce::String& key)
    {
        const auto keyPos = json.indexOf ("\"" + key + "\"");
        if (keyPos < 0) return {};
        const auto colon = json.indexOfChar (keyPos, ':');
        if (colon < 0) return {};
        const auto openQuote = json.indexOfChar (colon, '"');
        if (openQuote < 0) return {};
        const auto closeQuote = json.indexOfChar (openQuote + 1, '"');
        if (closeQuote < 0) return {};
        return json.substring (openQuote + 1, closeQuote);
    }

    double jsonNumber (const juce::String& json, const juce::String& key, double fallback)
    {
        const auto keyPos = json.indexOf ("\"" + key + "\"");
        if (keyPos < 0) return fallback;
        const auto colon = json.indexOfChar (keyPos, ':');
        if (colon < 0) return fallback;
        auto rest = json.substring (colon + 1).trim();
        auto token = rest.upToFirstOccurrenceOf (",", false, false)
                         .upToFirstOccurrenceOf ("}", false, false).trim();
        return token.getDoubleValue();
    }

    juce::String jstr (const juce::String& s)
    {
        juce::String out;
        for (auto c : s)
        {
            if (c == '"' || c == '\\') out << '\\';
            out << c;
        }
        return "\"" + out + "\"";
    }

    Manifest parseManifest (const juce::String& json)
    {
        Manifest m;
        m.outputDir = jsonString (json, "outputDir");
        m.version   = jsonString (json, "version");
        m.gitHead   = jsonString (json, "gitHead");

        int cursor = 0;
        for (;;)
        {
            const auto sp = json.indexOf (cursor, "\"path\"");
            if (sp < 0) break;

            const auto objStart = json.substring (0, sp).lastIndexOfChar ('{');
            const auto objEnd   = json.indexOfChar (sp, '}');
            if (objStart < 0 || objEnd < 0) break;

            const auto obj = json.substring (objStart, objEnd + 1);

            ManifestSource s;
            s.name     = jsonString (obj, "name");
            s.path     = jsonString (obj, "path");
            s.startSec = jsonNumber (obj, "startSec", 0.0);
            s.seconds  = jsonNumber (obj, "seconds", 12.0);

            if (s.path.isNotEmpty())
                m.sources.push_back (s);

            cursor = objEnd + 1;
        }

        return m;
    }
}

namespace
{

    //==========================================================================
    // Loading — WAV or FLAC; anything not already 48 kHz is resampled with a
    // deterministic Lagrange interpolator so every render runs at 48000 Hz.
    //==========================================================================
    bool loadStereo (const juce::File& file, juce::AudioBuffer<float>& dest,
                     double& sourceRate, bool& resampled)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            std::cerr << "  !! cannot open " << file.getFullPathName() << std::endl;
            return false;
        }

        const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
        const int numSamples  = (int) reader->lengthInSamples;

        dest.setSize (numChannels, numSamples);
        reader->read (&dest, 0, numSamples, 0, true, numChannels > 1);

        sourceRate = reader->sampleRate > 0 ? reader->sampleRate : kSampleRate;
        resampled  = std::abs (sourceRate - kSampleRate) > 0.5;

        if (resampled)
        {
            const double ratio = sourceRate / kSampleRate;   // input samples per output sample
            const int outLen   = (int) std::round ((double) numSamples / ratio);

            juce::AudioBuffer<float> rs (numChannels, outLen);
            for (int ch = 0; ch < numChannels; ++ch)
            {
                juce::LagrangeInterpolator interp;
                interp.reset();
                interp.process (ratio, dest.getReadPointer (ch), rs.getWritePointer (ch), outLen);
            }
            dest = std::move (rs);
        }

        // Mono sources run through both channels of the stereo engine.
        if (numChannels == 1)
        {
            const int n = dest.getNumSamples();
            dest.setSize (2, n, true, true, true);
            dest.copyFrom (1, 0, dest, 0, 0, n);
        }

        return true;
    }

    // Extract a mono-averaged copy, used for measurement only.
    std::vector<float> monoSum (const juce::AudioBuffer<float>& b)
    {
        std::vector<float> out ((size_t) b.getNumSamples(), 0.0f);
        const int n = b.getNumChannels();
        for (int ch = 0; ch < n; ++ch)
        {
            const auto* p = b.getReadPointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                out[(size_t) i] += p[i];
        }
        if (n > 1)
            for (auto& v : out) v /= (float) n;
        return out;
    }

    float peakDb (const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            peak = juce::jmax (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
        return juce::Decibels::gainToDecibels (peak, -120.0f);
    }

    float rmsDb (const juce::AudioBuffer<float>& b)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (0, 0, b.getNumSamples()), -120.0f);
    }

    //==========================================================================
    // Integrated loudness, ITU-R BS.1770-4 (K-weighting + gated mean of blocks).
    //==========================================================================
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    // K-weighting stage 1: high-shelf ("head") +12.2 dB above ~1.5 kHz.
    Biquad makeKStage1 (double fs)
    {
        Biquad f;
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0;
        f.b1 = 2.0 * (K * K - Vh) / a0;
        f.b2 = (Vh - Vb * K / Q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }

    // K-weighting stage 2: high-pass ~38 Hz.
    Biquad makeKStage2 (double fs)
    {
        Biquad f;
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        f.b0 = 1.0;
        f.b1 = -2.0;
        f.b2 = 1.0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }

    /** Integrated loudness in LUFS (absolute -0.691 dB offset, 400 ms blocks,
        75 % overlap, -70 LUFS absolute gate then -10 LU relative gate). */
    double integratedLufs (const juce::AudioBuffer<float>& buffer)
    {
        const auto mono = monoSum (buffer);
        if (mono.empty()) return -std::numeric_limits<double>::infinity();

        Biquad s1 = makeKStage1 (kSampleRate);
        Biquad s2 = makeKStage2 (kSampleRate);

        std::vector<double> weighted (mono.size());
        for (size_t i = 0; i < mono.size(); ++i)
            weighted[i] = s2.process (s1.process (mono[i]));

        const int blockLen = (int) std::round (0.400 * kSampleRate);   // 400 ms
        const int hopLen   = blockLen / 4;                             // 75 % overlap

        std::vector<double> loudness;
        for (size_t start = 0; start + (size_t) blockLen <= weighted.size();
             start += (size_t) hopLen)
        {
            double sum = 0.0;
            for (int i = 0; i < blockLen; ++i)
            {
                const double v = weighted[start + (size_t) i];
                sum += v * v;
            }
            const double meanSquare = sum / (double) blockLen;
            loudness.push_back (-0.691 + 10.0 * std::log10 (meanSquare + 1e-30));
        }

        if (loudness.empty()) return -std::numeric_limits<double>::infinity();

        // absolute gate at -70 LUFS
        double sum = 0.0; int count = 0;
        for (double l : loudness)
            if (l > -70.0) { sum += std::pow (10.0, l / 10.0); ++count; }
        if (count == 0) return -std::numeric_limits<double>::infinity();

        const double relativeGate = -0.691 + 10.0 * std::log10 (sum / count) - 10.0;

        sum = 0.0; count = 0;
        for (double l : loudness)
            if (l > -70.0 && l > relativeGate) { sum += std::pow (10.0, l / 10.0); ++count; }
        if (count == 0) return -std::numeric_limits<double>::infinity();

        return -0.691 + 10.0 * std::log10 (sum / count);
    }
}

namespace
{
    //==========================================================================
    // Block-based render through the real plugin.
    //==========================================================================
    void renderThrough (ZTameAudioProcessor& proc, juce::AudioBuffer<float>& buffer,
                        bool bypassed)
    {
        juce::MidiBuffer midi;
        for (int offset = 0; offset < buffer.getNumSamples(); offset += kBlockSize)
        {
            const int n = juce::jmin (kBlockSize, buffer.getNumSamples() - offset);

            juce::AudioBuffer<float> block (buffer.getNumChannels(), n);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                block.copyFrom (ch, 0, buffer, ch, offset, n);

            if (bypassed)
            {
                proc.processBlockBypassed (block, midi);
            }
            else
            {
                // Keep the plugin's own bypass flag off so processBlock runs the engine.
                proc.processBlock (block, midi);
            }

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, offset, block, ch, 0, n);
        }
    }

    void setParam (ZTameAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getValueTreeState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** The v1.2 showcase setting used for all real-instrument renders. */
    struct Settings
    {
        float sensitivity = 65.0f;
        float selectivity = 50.0f;
        float rangeDb     = 12.0f;
        float attackMs    = 8.0f;
        float releaseMs   = 180.0f;
        float dryWet      = 100.0f;
        bool  midSide     = false;
    };

    void applySettings (ZTameAudioProcessor& p, const Settings& s)
    {
        setParam (p, "bypass",      0.0f);
        setParam (p, "listenMode",  0.0f);
        setParam (p, "sidechain",   0.0f);
        setParam (p, "sensitivity", s.sensitivity);
        setParam (p, "selectivity", s.selectivity);
        setParam (p, "range",       s.rangeDb);
        setParam (p, "attack",      s.attackMs);
        setParam (p, "release",     s.releaseMs);
        setParam (p, "dryWet",      s.dryWet);
        setParam (p, "focusLow",    20.0f);
        setParam (p, "focusHigh",   20000.0f);
        setParam (p, "stereoLink",  1.0f);
        setParam (p, "processingMode", s.midSide ? 1.0f : 0.0f);
    }

    //==========================================================================
    bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& buffer)
    {
        file.deleteFile();   // FileOutputStream appends — always start clean

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (new juce::FileOutputStream (file), kSampleRate,
                                    (unsigned int) buffer.getNumChannels(),
                                    24, {}, 0));

        if (writer == nullptr)
        {
            std::cerr << "  !! failed to create " << file.getFullPathName() << std::endl;
            return false;
        }

        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    /** Extract [startSec, startSec+seconds) into a fresh buffer. */
    bool extractSegment (const juce::AudioBuffer<float>& src, double startSec,
                         double seconds, juce::AudioBuffer<float>& dst)
    {
        const int start = (int) std::round (startSec * kSampleRate);
        const int len   = (int) std::round (seconds * kSampleRate);

        if (start < 0 || len <= 0 || start + len > src.getNumSamples())
            return false;

        dst.setSize (src.getNumChannels(), len);
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            dst.copyFrom (ch, 0, src, ch, start, len);

        return true;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 2)
    {
        std::cerr << "usage: ZTame_RealRender <manifest.json>" << std::endl;
        return 2;
    }

    const juce::File manifestFile { juce::String (argv[1]) };
    if (! manifestFile.existsAsFile())
    {
        std::cerr << "manifest not found: " << manifestFile.getFullPathName() << std::endl;
        return 2;
    }

    const Manifest manifest = parseManifest (manifestFile.loadFileAsString());

    juce::File outDir (manifest.outputDir);
    outDir.createDirectory();

    const Settings settings;

    std::cout << "ZTame real-instrument render" << std::endl;
    std::cout << "  version : " << manifest.version << std::endl;
    std::cout << "  gitHead : " << manifest.gitHead << std::endl;
    std::cout << "  outDir  : " << outDir.getFullPathName() << std::endl;
    std::cout << "  settings: sensitivity=" << settings.sensitivity
              << " selectivity=" << settings.selectivity
              << " range=" << settings.rangeDb << "dB"
              << " attack=" << settings.attackMs << "ms"
              << " release=" << settings.releaseMs << "ms"
              << " dryWet=" << settings.dryWet << "%" << std::endl;
    std::cout << std::endl;

    juce::StringArray manifestLines;
    manifestLines.add ("# ZTame real-instrument A/B render manifest");
    manifestLines.add ("");
    manifestLines.add ("- plugin: ZTame");
    manifestLines.add ("- version: " + manifest.version);
    manifestLines.add ("- gitHead: " + manifest.gitHead);
    manifestLines.add ("- sampleRate: 48000");
    manifestLines.add ("- bitDepth: 24");
    manifestLines.add ("- blockSize: 512");
    manifestLines.add ("- settings: sensitivity=" + juce::String (settings.sensitivity)
                       + ", selectivity=" + juce::String (settings.selectivity)
                       + ", rangeDb=" + juce::String (settings.rangeDb)
                       + ", attackMs=" + juce::String (settings.attackMs)
                       + ", releaseMs=" + juce::String (settings.releaseMs)
                       + ", dryWet=" + juce::String (settings.dryWet));
    manifestLines.add ("");
    manifestLines.add ("| source | dry LUFS | wet LUFS | target LUFS | dry gain dB | wet gain dB | matched delta LU | latency samples |");
    manifestLines.add ("|---|---|---|---|---|---|---|");

    juce::StringArray statusLines;
    juce::StringArray metricsEntries;
    int rendered = 0;

    for (const auto& source : manifest.sources)
    {
        std::cout << "== " << source.name << std::endl;

        juce::AudioBuffer<float> whole;
        double sourceRate = 0.0;
        bool   resampled  = false;
        if (! loadStereo (juce::File (source.path), whole, sourceRate, resampled))
        {
            const auto msg = source.name + ": FAILED to load source file " + juce::String (source.path);
            std::cerr << "  !! " << msg << std::endl;
            statusLines.add ("- **FAILED** " + msg);
            continue;
        }

        juce::AudioBuffer<float> segment;
        if (! extractSegment (whole, source.startSec, source.seconds, segment))
        {
            const auto msg = source.name + ": FAILED to extract "
                           + juce::String (source.startSec, 2) + "s +"
                           + juce::String (source.seconds, 2)
                           + "s (file is " + juce::String (whole.getNumSamples() / kSampleRate, 2) + "s long)";
            std::cerr << "  !! " << msg << std::endl;
            statusLines.add ("- **FAILED** " + msg);
            continue;
        }

        // ------------------------------------------------------------------
        // Wet pass: a fresh engine per source keeps renders deterministic.
        // ------------------------------------------------------------------
        auto* wetProc = new ZTameAudioProcessor();
        wetProc->prepareToPlay (kSampleRate, kBlockSize);
        applySettings (*wetProc, settings);

        const int latency = wetProc->getLatencySamples();

        juce::AudioBuffer<float> wet;
        wet.makeCopyOf (segment);
        renderThrough (*wetProc, wet, false);

        // The wet MASTER: taken before any measurement-driven gain change, so
        // canonical evidence is never altered by the loudness match.
        juce::AudioBuffer<float> wetRaw;
        wetRaw.makeCopyOf (wet);

        // ------------------------------------------------------------------
        // Dry pass: the SAME engine run through processBlockBypassed, so the
        // reference carries identical latency and both files are sample-aligned.
        // ------------------------------------------------------------------
        auto* dryProc = new ZTameAudioProcessor();
        dryProc->prepareToPlay (kSampleRate, kBlockSize);
        applySettings (*dryProc, settings);

        juce::AudioBuffer<float> dry;
        dry.makeCopyOf (segment);
        renderThrough (*dryProc, dry, true);

        // ------------------------------------------------------------------
        // Fair A/B (min-target convention): BOTH comparison copies are levelled
        // to the QUIETER side's integrated loudness, so neither side can sound
        // better through a level bias and no added gain pushes peaks above the
        // master renders. Only the comparison copies carry this recorded
        // transformation; the masters stay untouched.
        // ------------------------------------------------------------------
        const double dryLufs = integratedLufs (dry);
        const double wetLufsBefore = integratedLufs (wet);
        const float wetRawPeak = peakDb (wetRaw);

        double targetLufs = 0.0;
        if (std::isfinite (dryLufs) && std::isfinite (wetLufsBefore))
            targetLufs = juce::jmin (dryLufs, wetLufsBefore);

        const double dryGainDb = std::isfinite (dryLufs) ? (targetLufs - dryLufs) : 0.0;
        const double wetGainDb = std::isfinite (wetLufsBefore) ? (targetLufs - wetLufsBefore) : 0.0;

        juce::AudioBuffer<float> dryCmp;
        dryCmp.makeCopyOf (dry);
        if (std::abs (dryGainDb) > 0.001)
            dryCmp.applyGain (juce::Decibels::decibelsToGain ((float) dryGainDb));

        juce::AudioBuffer<float> wetCmp;
        wetCmp.makeCopyOf (wetRaw);
        if (std::abs (wetGainDb) > 0.001)
            wetCmp.applyGain (juce::Decibels::decibelsToGain ((float) wetGainDb));

        const float dryPeak = peakDb (dry);
        const float dryCompPeak = peakDb (dryCmp);
        const float wetCompPeak = peakDb (wetCmp);
        const double dryCompLufs = integratedLufs (dryCmp);
        const double wetCompLufs = integratedLufs (wetCmp);
        const double matchedDeltaLu = (std::isfinite (dryCompLufs) && std::isfinite (wetCompLufs))
                                        ? (wetCompLufs - dryCompLufs) : 0.0;
        const float dryRms = rmsDb (dry);
        const float wetRms = rmsDb (wetRaw);

        const juce::String base (source.name);
        const auto dryFile    = outDir.getChildFile (base + "-A-dry.wav");
        const auto dryCmpFile = outDir.getChildFile (base + "-A-dry-compare.wav");
        const auto wetRawFile = outDir.getChildFile (base + "-B-wet-raw.wav");
        const auto wetCmpFile = outDir.getChildFile (base + "-B-wet.wav");

        const bool okDry    = writeWav24 (dryFile, dry);
        const bool okDryCmp = writeWav24 (dryCmpFile, dryCmp);
        const bool okWetRaw = writeWav24 (wetRawFile, wetRaw);
        const bool okWetCmp = writeWav24 (wetCmpFile, wetCmp);

        if (! okDry || ! okDryCmp || ! okWetRaw || ! okWetCmp)
        {
            statusLines.add ("- **FAILED** " + base + ": could not write WAV files");
            continue;
        }

        {
            juce::String e;
            e << "    {\n";
            e << "      \"name\": " << jstr (base) << ",\n";
            e << "      \"sourceFile\": " << jstr (juce::File (source.path).getFileName()) << ",\n";
            e << "      \"sourceSampleRate\": " << juce::String (sourceRate, 0) << ",\n";
            e << "      \"resampled\": " << (resampled ? "true" : "false") << ",\n";
            e << "      \"durationSec\": " << juce::String (segment.getNumSamples() / kSampleRate, 6) << ",\n";
            e << "      \"latencySamples\": " << latency << ",\n";
            e << "      \"dryMaster\": { \"file\": " << jstr (dryFile.getFileName())
              << ", \"peakDb\": " << juce::String (dryPeak, 4)
              << ", \"rmsDb\": " << juce::String (dryRms, 4)
              << ", \"lufs\": " << juce::String (dryLufs, 4) << " },\n";
            e << "      \"wetRawMaster\": { \"file\": " << jstr (wetRawFile.getFileName())
              << ", \"peakDb\": " << juce::String (wetRawPeak, 4)
              << ", \"rmsDb\": " << juce::String (wetRms, 4)
              << ", \"lufs\": " << juce::String (wetLufsBefore, 4) << " },\n";
            e << "      \"dryComparison\": { \"file\": " << jstr (dryCmpFile.getFileName())
              << ", \"peakDb\": " << juce::String (dryCompPeak, 4)
              << ", \"lufs\": " << juce::String (dryCompLufs, 4)
              << ", \"gainDb\": " << juce::String (dryGainDb, 4) << " },\n";
            e << "      \"wetComparison\": { \"file\": " << jstr (wetCmpFile.getFileName())
              << ", \"peakDb\": " << juce::String (wetCompPeak, 4)
              << ", \"lufs\": " << juce::String (wetCompLufs, 4)
              << ", \"gainDb\": " << juce::String (wetGainDb, 4) << " },\n";
            e << "      \"loudnessMatch\": { \"rawDeltaLu\": " << juce::String (wetLufsBefore - dryLufs, 4)
              << ", \"targetLufs\": " << juce::String (targetLufs, 4)
              << ", \"dryGainDb\": " << juce::String (dryGainDb, 4)
              << ", \"wetGainDb\": " << juce::String (wetGainDb, 4)
              << ", \"matchedDeltaLu\": " << juce::String (matchedDeltaLu, 4) << " }\n";
            e << "    }";
            metricsEntries.add (e);
        }

        std::cout << "  source " << juce::String (sourceRate, 0) << " Hz"
                  << (resampled ? " (resampled to 48000)" : " (native)") << std::endl;
        std::cout << "  dry  master  peak " << juce::String (dryPeak, 2) << " dBFS"
                  << "  LUFS " << juce::String (dryLufs, 2)
                  << "  rms " << juce::String (dryRms, 2) << std::endl;
        std::cout << "  wet  master  peak " << juce::String (wetRawPeak, 2) << " dBFS"
                  << "  LUFS " << juce::String (wetLufsBefore, 2)
                  << "  rms " << juce::String (wetRms, 2) << std::endl;
        std::cout << "  compare pair LUFS " << juce::String (targetLufs, 2)
                  << "  (gains dry " << juce::String (dryGainDb, 2)
                  << " dB / wet " << juce::String (wetGainDb, 2) << " dB)"
                  << "  matched delta " << juce::String (matchedDeltaLu, 3) << " LU" << std::endl;
        std::cout << "  latency " << latency << " samples" << std::endl;
        std::cout << "  wrote " << dryFile.getFileName() << ", " << dryCmpFile.getFileName() << ", "
                  << wetRawFile.getFileName() << ", " << wetCmpFile.getFileName() << std::endl;

        manifestLines.add ("| " + base
                           + " | " + juce::String (dryLufs, 2)
                           + " | " + juce::String (wetLufsBefore, 2)
                           + " | " + juce::String (targetLufs, 2)
                           + " | " + juce::String (dryGainDb, 2)
                           + " | " + juce::String (wetGainDb, 2)
                           + " | " + juce::String (matchedDeltaLu, 3)
                           + " | " + juce::String (latency) + " |");

        statusLines.add ("- OK " + base + " (masters + comparison pair written, latency "
                         + juce::String (latency) + " samples)");
        ++rendered;
    }

    manifestLines.add ("");
    manifestLines.add ("## Status");
    manifestLines.add ("");
    for (const auto& s : statusLines)
        manifestLines.add (s);
    manifestLines.add ("");
    manifestLines.add ("Rendered " + juce::String (rendered) + " of "
                       + juce::String ((int) manifest.sources.size()) + " requested sources.");

    const auto manifestOut = outDir.getChildFile ("MANIFEST.md");
    manifestOut.replaceWithText (manifestLines.joinIntoString ("\n"));

    // ---------------------------------------------------------------- metrics
    // Machine-readable record for automated validation (tools/release/
    // validate-audio.mjs): raw and matched loudness, peaks, trims, latency and
    // per-source sample rate. The manifest tool hashes these files; nothing
    // downstream may re-derive them.
    juce::StringArray metrics;
    metrics.add ("{");
    metrics.add ("  \"schemaVersion\": 1,");
    metrics.add ("  \"plugin\": \"ZTame\",");
    metrics.add ("  \"version\": " + jstr (manifest.version) + ",");
    metrics.add ("  \"gitHead\": " + jstr (manifest.gitHead) + ",");
    metrics.add ("  \"renderer\": \"ZTame_RealRender\",");
    metrics.add ("  \"renderSampleRate\": 48000,");
    metrics.add ("  \"renderBitDepth\": 24,");
    metrics.add ("  \"blockSize\": " + juce::String (kBlockSize) + ",");
    metrics.add ("  \"settings\": { \"sensitivity\": " + juce::String (settings.sensitivity, 2)
                 + ", \"selectivity\": " + juce::String (settings.selectivity, 2)
                 + ", \"rangeDb\": " + juce::String (settings.rangeDb, 2)
                 + ", \"attackMs\": " + juce::String (settings.attackMs, 2)
                 + ", \"releaseMs\": " + juce::String (settings.releaseMs, 2)
                 + ", \"dryWet\": " + juce::String (settings.dryWet, 2) + " },");
    metrics.add ("  \"sources\": [");
    metrics.add (metricsEntries.joinIntoString (",\n"));
    metrics.add ("  ]");
    metrics.add ("}");

    const auto metricsOut = outDir.getChildFile ("render-metrics.json");
    metricsOut.replaceWithText (metrics.joinIntoString ("\n") + "\n");

    std::cout << std::endl;
    std::cout << "Rendered " << rendered << " source(s). Manifest: "
              << manifestOut.getFullPathName() << std::endl;
    std::cout << "Metrics: " << metricsOut.getFullPathName() << std::endl;
    std::cout.flush();

    std::exit (0);   // audio processors are intentionally leaked (console teardown can hang)
}
