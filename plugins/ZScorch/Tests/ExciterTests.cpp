//==============================================================================
// ZScorch v2.0 — Exciter module tests (ADAA path, gating, bounded output)
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "Exciter.h"

using namespace ztest;

class ExciterTests : public juce::UnitTest
{
public:
    ExciterTests() : juce::UnitTest ("ZScorch Exciter") {}

    void runTest() override
    {
        beginTest ("Disabled exciter is a bit-exact bypass");
        {
            Exciter ex;
            ex.prepare (44100.0, 512, 2);

            juce::AudioBuffer<float> in = makeBuffer (2, 2048);
            fillSine (in, 44100.0, 8000.0f, 0.4f);

            juce::AudioBuffer<float> out;
            out.makeCopyOf (in, true);
            ex.process (out, 0.8f, 0.0f);

            expect (maxAbsDiff (in, out) == 0.0f, "gated off -> untouched");
        }

        beginTest ("Enabled exciter adds high-frequency harmonics");
        {
            Exciter ex;
            ex.prepare (44100.0, 4096, 1);

            juce::AudioBuffer<float> buf = makeBuffer (1, 8192);
            fillSine (buf, 44100.0, 6000.0f, 0.3f);

            ex.process (buf, 0.8f, 1.0f);

            const int fftOrder = 13;
            auto mag = spectrum (buf.getReadPointer (0), 1 << fftOrder, fftOrder);
            const int f0 = hzToBin (6000.0f, fftOrder, 44100.0);

            const float h3 = bandSum (mag, 3 * f0, 3);
            const float h1 = bandSum (mag, f0, 3);

            expectGreaterThan (h3, 1.0e-4f, "3rd harmonic generated");
            expectLessThan (h3, h1, "harmonic stays below the fundamental (musical)");
        }

        beginTest ("Exciter output is bounded and finite");
        {
            Exciter ex;
            ex.prepare (48000.0, 512, 2);

            juce::AudioBuffer<float> buf = makeBuffer (2, 8192);
            fillSine (buf, 48000.0, 9000.0f, 1.0f);   // hot, well past full scale

            ex.process (buf, 1.0f, 1.0f);

            expect (allFinite (buf), "finite");
            expectLessThan (peak (buf), 4.0f);
        }

        beginTest ("Exciter adds no latency");
        {
            Exciter ex;
            ex.prepare (44100.0, 512, 1);
            expectEquals (ex.getLatencySamples(), 0);
        }

        beginTest ("Enabling after bypass does not click the ADAA history");
        {
            Exciter ex;
            ex.prepare (44100.0, 512, 1);

            juce::AudioBuffer<float> buf = makeBuffer (1, 4096);
            fillSine (buf, 44100.0, 5000.0f, 0.5f);

            ex.process (buf, 0.0f, 0.0f);      // bypass
            ex.process (buf, 0.6f, 1.0f);      // engage

            expect (allFinite (buf), "finite after engage");

            const auto* d = buf.getReadPointer (0);
            float maxJump = 0.0f;
            for (int i = 1; i < buf.getNumSamples(); ++i)
                maxJump = juce::jmax (maxJump, std::abs (d[i] - d[i - 1]));
            expectLessThan (maxJump, 0.5f, "no discontinuity on engage");
        }
    }
};

static ExciterTests exciterTests;

void registerExciterTests() { juce::ignoreUnused (exciterTests); }
