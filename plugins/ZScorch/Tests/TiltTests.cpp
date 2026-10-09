//==============================================================================
// ZScorch v2.0 — Tilt module tests (spectral balance, transparency at 0 dB)
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "Tilt.h"

using namespace ztest;

class TiltTests : public juce::UnitTest
{
public:
    TiltTests() : juce::UnitTest ("ZScorch Tilt") {}

    void runTest() override
    {
        beginTest ("Zero tilt is near-transparent");
        {
            Tilt tilt;
            tilt.prepare (44100.0, 512, 2);

            juce::AudioBuffer<float> in = makeBuffer (2, 4096);
            fillSine (in, 44100.0, 500.0f, 0.4f);

            juce::AudioBuffer<float> out;
            out.makeCopyOf (in, true);
            tilt.process (out, 0.0f);

            expectLessThan (maxAbsDiff (in, out), 1.0e-4f, "0 dB tilt is a null");
        }

        beginTest ("Positive tilt brightens, negative tilt darkens");
        {
            auto bandEnergy = [] (juce::AudioBuffer<float>& buf, float hz, double sr)
            {
                const int fftOrder = 13;
                auto mag = spectrum (buf.getReadPointer (0), 1 << fftOrder, fftOrder);
                return bandSum (mag, hzToBin (hz, fftOrder, sr), 3);
            };

            const double sr = 44100.0;

            Tilt bright, dark;
            bright.prepare (sr, 8192, 1);
            dark.prepare (sr, 8192, 1);

            juce::AudioBuffer<float> lowB = makeBuffer (1, 8192), highB = makeBuffer (1, 8192);
            juce::AudioBuffer<float> lowD = makeBuffer (1, 8192), highD = makeBuffer (1, 8192);

            fillSine (lowB, sr, 200.0f, 0.5f);
            fillSine (highB, sr, 8000.0f, 0.5f);
            lowD.makeCopyOf (lowB, true);
            highD.makeCopyOf (highB, true);

            bright.process (lowB, 6.0f);
            bright.process (highB, 6.0f);
            dark.process (lowD, -6.0f);
            dark.process (highD, -6.0f);

            const float brightRatio = bandEnergy (highB, 8000.0f, sr) / juce::jmax (1.0e-9f, bandEnergy (lowB, 200.0f, sr));
            const float darkRatio   = bandEnergy (highD, 8000.0f, sr) / juce::jmax (1.0e-9f, bandEnergy (lowD, 200.0f, sr));

            expectGreaterThan (brightRatio, darkRatio * 1.5f,
                               "positive tilt is brighter than negative tilt");
        }

        beginTest ("Tilt stays finite across its range");
        {
            Tilt tilt;
            tilt.prepare (48000.0, 512, 2);

            for (float amount = -8.0f; amount <= 8.0f; amount += 0.5f)
            {
                juce::AudioBuffer<float> buf = makeBuffer (2, 1024);
                fillSine (buf, 48000.0, 1000.0f, 0.8f);
                tilt.process (buf, amount);
                expect (allFinite (buf), "finite at tilt " + juce::String (amount));
                expectLessThan (peak (buf), 2.0f);
            }
        }
    }
};

static TiltTests tiltTests;

void registerTiltTests() { juce::ignoreUnused (tiltTests); }
