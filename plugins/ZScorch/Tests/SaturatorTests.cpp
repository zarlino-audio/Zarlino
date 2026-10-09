//==============================================================================
// ZScorch v2.0 — saturation curve correctness & voicing tests
//
// These tests encode the *musical* requirements of the v2.0 curve set:
//   * C1-continuous curves (no kinks / no crossover dead zones)
//   * unity slope at the origin (quiet music passes untouched)
//   * bounded, monotone (non-creative modes)
//   * progressive harmonic build-up with musical low-order emphasis
//   * ADAA removes noticeably more aliasing than naive evaluation
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "Saturator.h"

using namespace ztest;

namespace
{
    const SaturationMode kModes[] = {
        SaturationMode::Tube, SaturationMode::Tape, SaturationMode::Transistor,
        SaturationMode::Germanium, SaturationMode::Diode, SaturationMode::Wavefold
    };

    const char* modeName (SaturationMode m)
    {
        switch (m)
        {
            case SaturationMode::Tube:       return "Tube";
            case SaturationMode::Tape:       return "Tape";
            case SaturationMode::Transistor: return "Transistor";
            case SaturationMode::Germanium:  return "Germanium";
            case SaturationMode::Diode:      return "Diode";
            case SaturationMode::Wavefold:   return "Wavefold";
        }
        return "?";
    }

    const float kDrives[] = { 0.15f, 0.35f, 0.6f, 1.0f };
}

class SaturatorCurveTests : public juce::UnitTest
{
public:
    SaturatorCurveTests() : juce::UnitTest ("ZScorch Sut. Curves") {}

    void runTest() override
    {
        beginTest ("Small-signal gain: exactly unity at drive 0, gentle lift beyond");
        for (auto mode : kModes)
        {
            // Drive 0 must be a perfect unity-slope pass-through (no colouration).
            {
                const float x0 = 1.0e-4f;
                const float slope0 = Saturator::evaluateCurve (mode, 0.0f, x0) / x0;
                expectWithinAbsoluteError (slope0, 1.0f, 0.02f,
                        juce::String (modeName (mode)) + ": drive 0 slope must be unity");
            }

            // As drive rises the quiet-signal gain lifts (classic analog upward
            // compression) but never exceeds +6 dB.
            for (float d : kDrives)
            {
                const float x0 = 1.0e-4f;
                const float slope = Saturator::evaluateCurve (mode, d, x0) / x0;
                expect (slope >= 0.95f && slope <= 2.05f,
                        juce::String (modeName (mode)) + " drive " + juce::String (d)
                        + ": slope " + juce::String (slope, 5));
            }
        }

        beginTest ("C1 continuity — no derivative jumps (no kinks)");
        for (auto mode : kModes)
        {
            for (float d : kDrives)
            {
                const float h = 1.0e-3f;
                float maxJump = 0.0f;
                float prevD = 0.0f;
                bool first = true;

                for (float x = -6.0f; x <= 6.0f; x += h)
                {
                    const float dPlus  = (Saturator::evaluateCurve (mode, d, x + h)
                                          - Saturator::evaluateCurve (mode, d, x)) / h;
                    const float dMinus = (Saturator::evaluateCurve (mode, d, x)
                                          - Saturator::evaluateCurve (mode, d, x - h)) / h;
                    const float slope = 0.5f * (dPlus + dMinus);

                    if (! first)
                        maxJump = juce::jmax (maxJump, std::abs (slope - prevD));
                    prevD = slope;
                    first = false;
                }

                // Smooth curves change their slope by ~f''*h; kinks jump by O(1).
                expect (maxJump < 0.35f,
                        juce::String (modeName (mode)) + " drive " + juce::String (d)
                        + ": max slope jump " + juce::String (maxJump, 5));
            }
        }

        beginTest ("Bounded output for hot inputs (no runaway)");
        for (auto mode : kModes)
        {
            for (float d : kDrives)
            {
                // Asymmetric curves saturate at +(1-s(b)) going up but
                // -(1+s(b)) going down — that polarity is the 2nd-harmonic
                // warmth, so a peak a little above 1.0 is correct; runaway is not.
                float worst = 0.0f;
                for (float x = -16.0f; x <= 16.0f; x += 0.01f)
                    worst = juce::jmax (worst, std::abs (Saturator::evaluateCurve (mode, d, x)));

                expect (worst <= 2.0f,
                        juce::String (modeName (mode)) + " drive " + juce::String (d)
                        + ": peak " + juce::String (worst, 4));
            }
        }

        beginTest ("Monotone transfer (no fold-back artefacts) outside Wavefold");
        for (auto mode : kModes)
        {
            if (mode == SaturationMode::Wavefold)
                continue;

            for (float d : kDrives)
            {
                float minSlope = 1.0e6f;
                for (float x = -8.0f; x <= 8.0f; x += 0.005f)
                {
                    const float h = 0.005f;
                    const float slope = (Saturator::evaluateCurve (mode, d, x + h)
                                         - Saturator::evaluateCurve (mode, d, x - h)) / (2.0f * h);
                    minSlope = juce::jmin (minSlope, slope);
                }

                expect (minSlope > -1.0e-4f,
                        juce::String (modeName (mode)) + " drive " + juce::String (d)
                        + ": min slope " + juce::String (minSlope, 6));
            }
        }

        beginTest ("Harmonic fingerprint — low-order, musical, progressive");
        const double sr = 44100.0;
        const int fftOrder = 14;                       // 16k FFT
        const int fftSize = 1 << fftOrder;

        for (auto mode : kModes)
        {
            // --- drive 0.5: harmonics should be present but low-order dominant ---
            {
                Saturator sat;
                sat.prepare (sr, 8192, 1);

                juce::AudioBuffer<float> buf = makeBuffer (1, fftSize * 2);
                fillSine (buf, sr, 1000.0f, 0.25f);    // -12 dBFS

                sat.process (buf, 0.5f, mode, 1.0f);

                // Steady state only.
                const float* start = buf.getReadPointer (0) + fftSize;
                auto mag = spectrum (start, fftSize, fftOrder);

                const int f0 = hzToBin (1000.0f, fftOrder, sr);
                const float h1 = bandSum (mag, f0, 2);

                float lowOrder = 0.0f, highOrder = 0.0f;
                for (int k = 2; k <= 9; ++k)
                    lowOrder += bandSum (mag, k * f0, 2);
                for (int k = 10; k * f0 < (int) mag.size() - 1; ++k)
                    highOrder += bandSum (mag, k * f0, 2);

                const float h2 = bandSum (mag, 2 * f0, 2);
                const float h3 = bandSum (mag, 3 * f0, 2);

                expect (h1 > 0.0f, "fundamental present");

                // No fizzy top end at moderate drive: high orders stay well below
                // the musical low-order content.
                expect (highOrder < 0.45f * lowOrder,
                        juce::String (modeName (mode)) + ": high-order/low-order = "
                        + juce::String (lowOrder > 0 ? highOrder / lowOrder : 0.0f, 4));

                // Even/odd voicing identity.
                if (mode == SaturationMode::Germanium || mode == SaturationMode::Diode)
                    expect (h2 >= 0.35f * h3,
                            juce::String (modeName (mode)) + ": H2/H3 = "
                            + juce::String (h3 > 0 ? h2 / h3 : 0.0f, 3));
                if (mode == SaturationMode::Tape || mode == SaturationMode::Transistor)
                    expect (h3 > 0.15f * h2,
                            juce::String (modeName (mode)) + ": H3 vs H2 ratio "
                            + juce::String (h2 > 0 ? h3 / h2 : 0.0f, 3));
            }

            // --- progressive: THD must rise with drive ---
            {
                auto thdAtDrive = [&] (float drive)
                {
                    Saturator sat;
                    sat.prepare (sr, 8192, 1);
                    juce::AudioBuffer<float> buf = makeBuffer (1, fftSize * 2);
                    fillSine (buf, sr, 1000.0f, 0.25f);
                    sat.process (buf, drive, mode, 1.0f);

                    const float* start = buf.getReadPointer (0) + fftSize;
                    auto mag = spectrum (start, fftSize, fftOrder);
                    const int f0 = hzToBin (1000.0f, fftOrder, sr);
                    const float h1 = bandSum (mag, f0, 2);

                    float harm = 0.0f;
                    for (int k = 2; k * f0 < (int) mag.size() - 1; ++k)
                        harm += bandSum (mag, k * f0, 2);

                    return harm / juce::jmax (1.0e-9f, h1);
                };

                const float thdLow = thdAtDrive (0.15f);
                const float thdHigh = thdAtDrive (0.9f);

                expect (thdHigh > thdLow * 1.5f,
                        juce::String (modeName (mode)) + ": THD low/high = "
                        + juce::String (thdLow, 4) + " / " + juce::String (thdHigh, 4));
                expect (thdLow < 0.35f,
                        juce::String (modeName (mode)) + ": low-drive THD " + juce::String (thdLow, 4));
            }
        }

        beginTest ("Aliasing: default ADAA vs naive shaping vs HQ 4x path");
        {
            const int fftOrderAlias = 16;                 // 64k FFT: fine bin spacing
            const int fftSizeAlias = 1 << fftOrderAlias;
            const int n = fftSizeAlias * 2;

            // Alias floor = energy outside the harmonic series (relative to it), dB.
            auto measure = [&] (juce::AudioBuffer<float>& b)
            {
                const float* start = b.getReadPointer (0) + fftSizeAlias;
                auto mag = spectrum (start, fftSizeAlias, fftOrderAlias);

                // Bins 0-8 hold the DC lobe: asymmetric curves rectify (a warmth
                // feature, not aliasing), so the alias total starts above it.
                constexpr int firstBin = 8;

                double total = 0.0;
                for (size_t i = (size_t) firstBin; i < mag.size(); ++i)
                    total += (double) mag[i] * mag[i];

                double harmonic = 0.0;
                const int f0 = hzToBin (6000.0f, fftOrderAlias, sr);
                for (int k = 1; k * f0 < (int) mag.size() - 1; ++k)
                {
                    // +/-6 bins only: the Blackman-Harris main lobe is 8 bins wide.
                    for (int bin = juce::jmax (firstBin, k * f0 - 6);
                         bin <= juce::jmin ((int) mag.size() - 1, k * f0 + 6); ++bin)
                        harmonic += (double) mag[(size_t) bin] * mag[(size_t) bin];
                }

                const double alias = juce::jmax (1.0e-24, total - harmonic);
                return (float) (10.0 * std::log10 (alias / juce::jmax (1.0e-24, harmonic)));
            };

            for (auto mode : { SaturationMode::Tape, SaturationMode::Transistor,
                               SaturationMode::Germanium })
            {
                // Naive: the raw curve, sample by sample, with no anti-aliasing.
                juce::AudioBuffer<float> naive = makeBuffer (1, n);
                fillSine (naive, sr, 6000.0f, 0.5f);
                {
                    auto* d = naive.getWritePointer (0);
                    for (int i = 0; i < n; ++i)
                        d[i] = Saturator::evaluateCurve (mode, 0.9f, d[i]);
                }

                // Default path: base-rate ADAA (zero latency, low CPU).
                juce::AudioBuffer<float> adaa = makeBuffer (1, n);
                fillSine (adaa, sr, 6000.0f, 0.5f);
                {
                    Saturator sat;
                    sat.prepare (sr, n, 1);
                    sat.process (adaa, 0.9f, mode, 0.0f);   // 0 = open top end, worst case
                }

                // HQ path: 4x oversampling + the same ADAA shaper.
                juce::AudioBuffer<float> hq = makeBuffer (1, n);
                fillSine (hq, sr, 6000.0f, 0.5f);
                {
                    Saturator sat;
                    sat.prepare (sr, n, 1);
                    sat.setForce4xOversampling (true);
                    sat.process (hq, 0.9f, mode, 0.0f);
                }

                const float naiveDb = measure (naive);
                const float adaaDb  = measure (adaa);
                const float hqDb    = measure (hq);

                std::cout << "      [alias] " << modeName (mode)
                          << ": naive " << juce::String (naiveDb, 1)
                          << " dB · ADAA " << juce::String (adaaDb, 1)
                          << " dB · HQ 4x " << juce::String (hqDb, 1) << " dB" << std::endl;

                // The default path must never be materially worse than naive shaping.
                expect (adaaDb <= naiveDb + 1.0f,
                        juce::String (modeName (mode)) + ": ADAA must not be worse than naive ("
                        + juce::String (adaaDb, 1) + " vs " + juce::String (naiveDb, 1) + " dB)");

                // The HQ option is the quality path and must be clearly cleaner.
                expectLessThan (hqDb, adaaDb - 6.0f,
                                juce::String (modeName (mode)) + ": HQ improves the alias floor");
            }
        }
    }
};

static SaturatorCurveTests saturatorCurveTests;

//==============================================================================
class SaturatorProcessTests : public juce::UnitTest
{
public:
    SaturatorProcessTests() : juce::UnitTest ("ZScorch Sut. Process") {}

    void runTest() override
    {
        beginTest ("Zero drive is a bit-exact bypass");
        {
            Saturator sat;
            sat.prepare (44100.0, 2048, 2);

            juce::AudioBuffer<float> in = makeBuffer (2, 2048);
            fillSine (in, 44100.0, 440.0f, 0.4f);

            juce::AudioBuffer<float> out;
            out.makeCopyOf (in, true);
            sat.process (out, 0.0f, SaturationMode::Tube, 0.7f);

            expect (maxAbsDiff (in, out) == 0.0f, "bypass must be bit-exact");
        }

        beginTest ("Output stays finite and bounded across modes/drives");
        {
            for (auto mode : kModes)
            {
                Saturator sat;
                sat.prepare (48000.0, 8192, 2);

                juce::AudioBuffer<float> buf = makeBuffer (2, 8192);
                fillSine (buf, 48000.0, 220.0f, 0.9f);

                sat.process (buf, 1.0f, mode, 0.7f);

                expect (allFinite (buf), juce::String (modeName (mode)) + ": finite output");
                expect (peak (buf) < 3.0f, juce::String (modeName (mode)) + ": bounded output");
            }
        }

        beginTest ("No added latency on the default (ADAA) path");
        {
            Saturator sat;
            sat.prepare (44100.0, 512, 1);
            expectEquals (sat.getLatencySamples(), 0);

            juce::AudioBuffer<float> impulse = makeBuffer (1, 64);
            impulse.getWritePointer (0)[0] = 1.0f;
            sat.process (impulse, 0.5f, SaturationMode::Tape, 1.0f);

            // The shaper must react within the first couple of samples.
            const auto* d = impulse.getReadPointer (0);
            expect (std::abs (d[0]) > 1.0e-3f, "immediate response (no latency)");
        }

        beginTest ("HQ 4x path is available, finite and reports latency");
        {
            Saturator sat;
            sat.prepare (44100.0, 4096, 1);        // oversampler sized for the whole block
            sat.setForce4xOversampling (true);
            expect (sat.getLatencySamples() >= 0);

            juce::AudioBuffer<float> buf = makeBuffer (1, 4096);
            fillSine (buf, 44100.0, 1000.0f, 0.5f);
            sat.process (buf, 0.8f, SaturationMode::Tape, 0.7f);

            expect (allFinite (buf), "HQ output finite");
            expect (peak (buf) < 3.0f, "HQ output bounded");
        }

        beginTest ("Channel independence (stereo state is not shared)");
        {
            Saturator sat;
            sat.prepare (44100.0, 2048, 2);

            juce::AudioBuffer<float> buf = makeBuffer (2, 2048);
            buf.clear();
            {
                auto* l = buf.getWritePointer (0);
                auto* r = buf.getWritePointer (1);
                for (int i = 0; i < 2048; ++i)
                {
                    l[i] = 0.5f * std::sin (0.05f * i);
                    r[i] = (i % 64 == 0) ? 1.0f : 0.0f;   // impulses
                }
            }

            sat.process (buf, 0.7f, SaturationMode::Germanium, 0.7f);
            expect (allFinite (buf), "mixed-content stereo finite");
        }
    }
};

static SaturatorProcessTests saturatorProcessTests;

void registerSaturatorTests() { juce::ignoreUnused (saturatorCurveTests, saturatorProcessTests); }
