//==============================================================================
// ZTame Offline Render Tool — reference implementation
//
// Renders real audio files and writes unprocessed (dry) and processed (wet)
// versions for the website A/B demos. The ZTame plugin's own FFT wet path
// currently has a level bug (see PluginProcessor), so this tool implements a
// clean reference of the documented algorithm:
//   Hann-windowed FFT (50% overlap), peak-vs-local-floor detection,
//   per-peak dynamic notch reduction, gain smoothing, latency-aligned dry/wet.
// When the plugin's DSP is fixed, re-render with the plugin.
//
// Usage: ZTame_OfflineRender <in.wav|.ogg> <dryOut.wav> <procOut.wav> <preset> [startSec] [durSec]
// Presets: vocal-soothe | room-tamer | default
//==============================================================================

#include <JuceHeader.h>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>
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
    auto writeU16 = [&] (juce::uint16 v) { for (int i = 0; i < 2; ++i) fos.writeByte ((char) ((v >> (8 * i)) & 0xff)); };

    fos.write ("RIFF", 4);  writeU32 ((juce::uint32) riffSize);  fos.write ("WAVE", 4);
    fos.write ("fmt ", 4);  writeU32 (16);
    writeU16 (1);           // PCM
    writeU16 ((juce::uint16) ch);
    writeU32 ((juce::uint32) sr);
    writeU32 ((juce::uint32) (sr * ch * 2));
    writeU16 ((juce::uint16) (ch * 2));
    writeU16 (16);
    fos.write ("data", 4);  writeU32 ((juce::uint32) dataBytes);

    // Interleave and convert to 16-bit.
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < ch; ++c)
        {
            float s = buf.getSample (c, f);
            s = juce::jlimit (-1.0f, 1.0f, s);
            short v = (short) (s * 32767.0f);
            writeU16 ((juce::uint16) v);
        }

    return true;
}

//--------------------------------------------------------------------------
// Reference spectral resonance suppressor.
//--------------------------------------------------------------------------
struct SpectralSuppressor
{
    static constexpr int fftSize = 2048;
    static constexpr int half = fftSize / 2;
    static constexpr int numBins = half + 1;
    static constexpr int hop = fftSize / 2;

    float sensitivity = 0.38f;   // 0..1
    float rangeDb = 9.0f;        // max reduction
    float selectivity = 0.6f;    // 0..1
    float dryWet = 0.65f;        // 0..1
    float attackMs = 3.0f, releaseMs = 150.0f;
    bool forcePassthru = false;  // diagnostic: apply unity gains

    std::vector<float> window;
    std::vector<float> norm;        // exact per-position OLA normalisation
    std::vector<float> binGain;     // current smoothed per-bin gain
    juce::dsp::FFT fft { 11 };
    std::vector<float> fftData;     // 2*fftSize floats (interleaved real/imag)

    float attackCoef = 0.0f, releaseCoef = 0.0f;

    void reset (double sr)
    {
        // PERIODIC Hann.
        window.resize (fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * i / fftSize));

        // EXACT overlap-add normalisation. For 50% overlap every output
        // position is covered by two frames; the sum of window^2 over those
        // two is NOT constant for a Hann (it varies with position, period
        // hop), so we normalise each position by the exact inverse sum.
        norm.resize (hop);
        for (int m = 0; m < hop; ++m)
        {
            float s = 0.0f;
            for (int f = m; f < fftSize; f += hop) s += window[f] * window[f];
            norm[m] = 1.0f / (s + 1e-12f);
        }

        binGain.assign (numBins, 1.0f);
        fftData.assign (2 * fftSize, 0.0f);

        // Smoothing coefficients applied once per FRAME (hop samples apart):
        // time constant in frames = (ms/1000)*sr/hop.
        const float framesPerSec = (float) sr / (float) hop;
        attackCoef  = 1.0f - std::exp (-1.0f / ((attackMs  / 1000.0f) * framesPerSec));
        releaseCoef = 1.0f - std::exp (-1.0f / ((releaseMs / 1000.0f) * framesPerSec));
    }

    // Process a whole channel in one call (offline). Latency = fftSize:
    // out[j] is the processed version of in[j - fftSize]. This is a plain
    // block-based STFT — no ring-buffer state, so the alignment is exact.
    void process (const float* in, float* out, int n)
    {
        std::vector<float> acc (n + fftSize, 0.0f);
        const int numFrames = n / hop + 2;

        for (int k = 0; k < numFrames; ++k)
        {
            const int start = k * hop;

            // ---- Analysis: window the fftSize samples starting at `start`.
            // The real-only forward transform reads the FIRST HALF of the
            // buffer as size CONSECUTIVE real samples (d[0..fftSize-1]) — do
            // NOT interleave with zeros (that zero-stuffs the signal).
            for (int i = 0; i < fftSize; ++i)
            {
                const int idx = start + i;
                const float x = (idx < n) ? in[idx] : 0.0f;
                fftData[i] = x * window[i];
            }

            fft.performRealOnlyForwardTransform (fftData.data(), false);

            // ---- Magnitudes for the non-negative bins (0..numBins-1).
            float mag[numBins];
            for (int i = 0; i < numBins; ++i)
            {
                const float re = fftData[2 * i], im = fftData[2 * i + 1];
                mag[i] = std::sqrt (re * re + im * im);
            }

            // ---- Peak detection: thresholds (dB) from the knobs.
            //  sensitivity -> base prominence required (5..15 dB)
            //  selectivity -> how sharply defined the peak must be (8..16 dB)
            const float threshDb  = 5.0f + sensitivity * 10.0f;
            const float sharpness = 8.0f + selectivity * 8.0f;

            float gains[numBins];
            gains[0] = gains[numBins - 1] = 1.0f;   // DC & Nyquist untouched

            if (forcePassthru)
            {
                for (int i = 1; i < numBins - 1; ++i) gains[i] = 1.0f;
            }
            else for (int i = 1; i < numBins - 1; ++i)
            {
                // Local floor = average of 7 neighbours around the bin.
                const int lo = std::max (1, i - 3), hi = std::min (numBins - 2, i + 3);
                int cnt = 0; float floor = 0.0f;
                for (int q = lo; q <= hi; ++q) { if (q != i) { floor += mag[q]; ++cnt; } }
                floor /= (float) cnt;

                const float floorDb = 20.0f * std::log10 (floor + 1e-30f);
                const float magDb   = 20.0f * std::log10 (mag[i] + 1e-30f);
                const float promDb  = magDb - floorDb;

                const bool isLocalMax = mag[i] > mag[i - 1] && mag[i] >= mag[i + 1];
                const bool isSharp = promDb > sharpness;

                if (isLocalMax && isSharp && promDb > threshDb)
                {
                    // Dynamic reduction — full depth only for very prominent
                    // isolated peaks; gentle for borderline ones.
                    float depth = rangeDb * 0.75f * std::min (1.0f, (promDb - threshDb) / 10.0f);
                    float g = std::pow (10.0f, -depth / 20.0f);
                    gains[i] = g;
                }
                else
                {
                    gains[i] = 1.0f;
                }
            }

            // ---- Time-smooth the gains (attack fast, release slow).
            for (int i = 0; i < numBins; ++i)
            {
                const float target = gains[i];
                if (target < binGain[i])
                    binGain[i] += (target - binGain[i]) * attackCoef;
                else
                    binGain[i] += (target - binGain[i]) * releaseCoef;
            }

            // ---- Apply gains symmetrically (Hermitian) to all bins.
            for (int i = 0; i < numBins; ++i)
            {
                fftData[2 * i]     *= binGain[i];
                fftData[2 * i + 1] *= binGain[i];
                if (i > 0 && i < half)
                {
                    const int m = fftSize - i;
                    fftData[2 * m]     *= binGain[i];
                    fftData[2 * m + 1] *= binGain[i];
                }
            }

            fft.performRealOnlyInverseTransform (fftData.data());

            // ---- Synthesis: frame time i -> output time start+i+fftSize.
            // The FFT inverse is already normalised by 1/fftSize; the exact
            // per-position OLA normalisation is applied at read-out.
            for (int i = 0; i < fftSize; ++i)
            {
                const int o = start + i + fftSize;
                if (o < (int) acc.size())
                    acc[o] += fftData[i] * window[i];
            }
        }

        for (int j = 0; j < n; ++j)
            out[j] = acc[j] * norm[j % hop];
    }
};

//--------------------------------------------------------------------------
// Presets
//--------------------------------------------------------------------------
static void applyPreset (SpectralSuppressor& dsp, const String& preset)
{
    if (preset == "vocal-soothe")
    {
        dsp.sensitivity = 0.38f;
        dsp.rangeDb = 9.0f;
        dsp.selectivity = 0.6f;
        dsp.dryWet = 0.65f;
        dsp.attackMs = 3.0f;
        dsp.releaseMs = 150.0f;
    }
    else if (preset == "room-tamer")
    {
        dsp.sensitivity = 0.45f;
        dsp.rangeDb = 7.0f;
        dsp.selectivity = 0.4f;
        dsp.dryWet = 0.80f;
        dsp.attackMs = 5.0f;
        dsp.releaseMs = 250.0f;
    }
    else if (preset == "master-polish")
    {
        // Full-mix polish: catches more, broader peaks, gentle depth and a
        // slow release so the mix stays stable while resonances are tamed.
        dsp.sensitivity = 0.5f;
        dsp.rangeDb = 6.0f;
        dsp.selectivity = 0.35f;
        dsp.dryWet = 0.55f;
        dsp.attackMs = 6.0f;
        dsp.releaseMs = 400.0f;
    }
    else
    {
        dsp.sensitivity = 0.4f;
        dsp.rangeDb = 8.0f;
        dsp.selectivity = 0.5f;
        dsp.dryWet = 0.7f;
        dsp.attackMs = 4.0f;
        dsp.releaseMs = 150.0f;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc < 5)
    {
        std::cerr << "Usage: ZTame_OfflineRender <in> <dryOut> <procOut> <preset> [startSec] [durSec]\n";
        return 1;
    }

    const juce::File inFile  { juce::String (argv[1]) };
    const juce::File dryFile { juce::String (argv[2]) };
    const juce::File wetFile { juce::String (argv[3]) };
    const juce::String preset { argv[4] };

    // --selftest: run a sine and an impulse through the engine (dryWet=1).
    if (preset == "selftest")
    {
        const int n = 48000 * 2;   // 2 s @ 48k
        for (int mode = 0; mode < 2; ++mode)
        {
            SpectralSuppressor sd;
            sd.dryWet = 1.0f;          // full wet
            sd.forcePassthru = (mode == 0);
            sd.reset (48000.0);

            std::vector<float> in (n), out (n);
            double inSq = 0.0, outSq = 0.0;
            for (int i = 0; i < n; ++i)
            {
                in[i] = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
                inSq += (double) in[i] * in[i];
            }
            sd.process (in.data(), out.data(), n);   // whole channel in one call
            for (int i = 0; i < n; ++i) outSq += (double) out[i] * out[i];
            const double inRms = std::sqrt (inSq / n), outRms = std::sqrt (outSq / n);
            float avgGain = 0.0f;
            for (int i = 0; i < SpectralSuppressor::numBins; ++i) avgGain += sd.binGain[i];
            avgGain /= (float) SpectralSuppressor::numBins;
            std::cout << "SELFTEST[" << (mode == 0 ? "passthru" : "detect")
                      << "] inRMS=" << 20.0 * std::log10 (inRms + 1e-30)
                      << " outRMS=" << 20.0 * std::log10 (outRms + 1e-30)
                      << " avgGain=" << 20.0f * std::log10 (avgGain + 1e-30f) << "dB"
                      << " (sin 440Hz 0.25 amp, dryWet=1)\n";

            // Impulse test: a 1.0 impulse should come out as a 1.0 impulse
            // at sample fftSize (the engine latency), showing the exact
            // pipeline gain.
            SpectralSuppressor si;
            si.dryWet = 1.0f;
            si.forcePassthru = (mode == 0);
            si.reset (48000.0);
            std::vector<float> im (n, 0.0f), om (n, 0.0f);
            im[0] = 1.0f;
            si.process (im.data(), om.data(), n);
            float peak = 0.0f; int peakPos = -1;
            for (int i = 0; i < n; ++i)
                if (std::fabs (om[i]) > peak) { peak = std::fabs (om[i]); peakPos = i; }
            std::cout << "  IMPULSE peak=" << peak << " at " << peakPos
                      << " (expect 1.0 at " << SpectralSuppressor::fftSize << ")\n";
        }

        // Direct FFT round-trip: forward then inverse with NO modification.
        // The Fallback engine normalises the inverse by 1/fftSize, so the
        // round trip must be ~identity.
        {
            juce::dsp::FFT rtf (11);
            std::vector<float> rt (2 * SpectralSuppressor::fftSize, 0.0f);
            double rtInSq = 0.0, rtOutSq = 0.0;
            for (int i = 0; i < SpectralSuppressor::fftSize; ++i)
            {
                rt[i] = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * i / 48000.0f);
                rtInSq += (double) rt[i] * rt[i];
            }
            rtf.performRealOnlyForwardTransform (rt.data(), false);
            rtf.performRealOnlyInverseTransform (rt.data());
            for (int i = 0; i < SpectralSuppressor::fftSize; ++i)
                rtOutSq += (double) rt[i] * rt[i];
            std::cout << "  FFT-RT inRMS=" << 20.0 * std::log10 (std::sqrt (rtInSq / SpectralSuppressor::fftSize) + 1e-30)
                      << " outRMS=" << 20.0 * std::log10 (std::sqrt (rtOutSq / SpectralSuppressor::fftSize) + 1e-30)
                      << " (expect equal)\n";
        }
        return 0;
    }

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

    SpectralSuppressor dsp;
    applyPreset (dsp, preset);
    dsp.reset (sr);

    juce::AudioBuffer<float> processed (2, totalSamples);
    processed.clear();

    // Wet channels (full length) — the block engine must see the whole
    // channel in one call (its output carries an fftSize latency).
    std::vector<float> wetL (totalSamples), wetR (totalSamples);
    {
        std::vector<float> inL (totalSamples), inR (totalSamples);
        for (int i = 0; i < totalSamples; ++i)
        {
            inL[i] = input.getSample (0, i);
            inR[i] = input.getSample (1, i);
        }
        dsp.process (inL.data(), wetL.data(), totalSamples);
        dsp.process (inR.data(), wetR.data(), totalSamples);
    }

    // Mix, latency-aligned: wet[j] is the processed version of in[j-fftSize],
    // so the wet sample matching dry[i] is wet[i+fftSize].
    const int lat = SpectralSuppressor::fftSize;
    for (int i = 0; i < totalSamples; ++i)
    {
        const int w = i + lat;
        const float wL = (w < totalSamples) ? wetL[w] : 0.0f;
        const float wR = (w < totalSamples) ? wetR[w] : 0.0f;
        const float dryL = input.getSample (0, i);
        const float dryR = input.getSample (1, i);
        processed.setSample (0, i, dryL * (1.0f - dsp.dryWet) + wL * dsp.dryWet);
        processed.setSample (1, i, dryR * (1.0f - dsp.dryWet) + wR * dsp.dryWet);
    }

    if (! writePcmWav (wetFile, processed, sr))
    {
        std::cerr << "Cannot write processed output.\n";
        return 4;
    }

    std::cout << "OK preset=" << preset << " samples=" << totalSamples
              << " rate=" << (int) sr << "\n";
    return 0;
}
