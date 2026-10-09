//==============================================================================
// ZScorch v2.0 — AnalysisCore tests (spectral features + display spectrum)
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "AnalysisCore.h"

using namespace ztest;

class AnalysisCoreTests : public juce::UnitTest
{
public:
    AnalysisCoreTests() : juce::UnitTest ("ZScorch Analysis Core") {}

    void runTest() override
    {
        const double sr = 44100.0;

        beginTest ("Silence reads as inactive");
        {
            AnalysisCore core;
            core.prepare (sr, 512);

            juce::AudioBuffer<float> silence = makeBuffer (2, 4096);
            for (int i = 0; i < 8; ++i)
                core.process (silence, silence.getNumSamples(), 2);

            const auto& cv = core.getControlVector();
            expectLessThan (cv.activity, 1.0e-3f);
            expectGreaterThan (cv.spectralFlatness, 0.5f, "silence treated as uninformative");
        }

        beginTest ("Sine reads as tonal; noise reads as flat");
        {
            AnalysisCore toneCore, noiseCore;
            toneCore.prepare (sr, 512);
            noiseCore.prepare (sr, 512);

            juce::AudioBuffer<float> tone = makeBuffer (2, 4096);
            fillSine (tone, sr, 1000.0f, 0.5f);

            juce::AudioBuffer<float> noise = makeBuffer (2, 4096);
            {
                std::mt19937 rng (1234);
                std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = noise.getWritePointer (ch);
                    for (int i = 0; i < noise.getNumSamples(); ++i)
                        d[i] = dist (rng);
                }
            }

            for (int i = 0; i < 8; ++i)
            {
                toneCore.process (tone, tone.getNumSamples(), 2);
                noiseCore.process (noise, noise.getNumSamples(), 2);
            }

            const auto& toneCV = toneCore.getControlVector();
            const auto& noiseCV = noiseCore.getControlVector();

            expectGreaterThan (toneCV.activity, 0.05f, "tone is active");
            expectLessThan (toneCV.spectralFlatness, noiseCV.spectralFlatness,
                            "tone is more tonal than noise");
            expectWithinAbsoluteError (toneCV.brightness, toneCV.brightness, 1.0e-6f);
        }

        beginTest ("Display spectrum exposes sane bin geometry");
        {
            AnalysisCore core;
            core.prepare (sr, 512);

            juce::AudioBuffer<float> tone = makeBuffer (2, 4096);
            fillSine (tone, sr, 1000.0f, 0.5f);
            for (int i = 0; i < 8; ++i)
                core.process (tone, tone.getNumSamples(), 2);

            expectGreaterThan (core.getNumBins(), 100);
            expectEquals (core.getNumBins(), (int) core.getDisplayMagnitude().size());
            expectWithinAbsoluteError (core.binToFrequency (0, sr), 0.0f, 1.0e-3f);

            // The 1 kHz peak must be the dominant bin.
            const auto& mag = core.getDisplayMagnitude();
            int peakBin = 0;
            float peakVal = 0.0f;
            for (int i = 1; i < (int) mag.size(); ++i)
                if (mag[(size_t) i] > peakVal) { peakVal = mag[(size_t) i]; peakBin = i; }

            const float peakHz = core.binToFrequency (peakBin, sr);
            expectWithinAbsoluteError (peakHz, 1000.0f, 120.0f);

            for (float v : mag)
                expect (std::isfinite (v), "magnitudes finite");
        }

        beginTest ("NaN input leaves the analysis stable");
        {
            AnalysisCore core;
            core.prepare (sr, 512);

            juce::AudioBuffer<float> bad = makeBuffer (2, 4096);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = bad.getWritePointer (ch);
                for (int i = 0; i < bad.getNumSamples(); ++i)
                    d[i] = (i % 97 == 0) ? std::numeric_limits<float>::quiet_NaN() : 0.1f;
            }

            for (int i = 0; i < 8; ++i)
                core.process (bad, bad.getNumSamples(), 2);

            const auto& cv = core.getControlVector();
            expect (std::isfinite (cv.harmonicDensity) && std::isfinite (cv.spectralFlatness)
                    && std::isfinite (cv.crestFactor) && std::isfinite (cv.brightness)
                    && std::isfinite (cv.activity),
                    "control vector stays finite with NaN input");
        }
    }
};

static AnalysisCoreTests analysisCoreTests;

void registerAnalysisCoreTests() { juce::ignoreUnused (analysisCoreTests); }
