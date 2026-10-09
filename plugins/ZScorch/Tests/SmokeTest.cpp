//==============================================================================
// ZScorch v2.0 — smoke tests: instantiation, basic audio flow, CPU sanity,
// spectrum/meter interfaces used by the editor.
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"

using namespace ztest;

class SmokeTests : public juce::UnitTest
{
public:
    SmokeTests() : juce::UnitTest ("ZScorch Smoke") {}

    void runTest() override
    {
        beginTest ("Processor instantiates with the full v2 parameter set");
        {
            ZScorchAudioProcessor proc;
            auto& vts = proc.getValueTreeState();

            const char* ids[] = {
                "lift", "character", "mix",
                "satDriveLow", "satDriveLoMid", "satDriveHiMid", "satDriveHigh",
                "satModeLow", "satModeLoMid", "satModeHiMid", "satModeHigh",
                "satHarmRedLow", "satHarmRedLoMid", "satHarmRedHiMid", "satHarmRedHigh",
                "tiltAmount", "exciterAmount", "exciterEnabled",
                "bandCount",
                "bandGainLow", "bandGainLoMid", "bandGainHiMid", "bandGainHigh",
                "preEqLowShelfFreq", "preEqLowShelfGain", "preEqBellFreq",
                "preEqBellGain", "preEqBellQ", "preEqHighShelfFreq", "preEqHighShelfGain",
                "postEqLowShelfFreq", "postEqLowShelfGain", "postEqBellFreq",
                "postEqBellGain", "postEqBellQ", "postEqHighShelfFreq", "postEqHighShelfGain",
                "outputCeiling", "oversample4x"
            };

            for (auto* id : ids)
                expect (vts.getParameter (id) != nullptr, juce::String ("missing parameter: ") + id);

            expect (proc.hasEditor(), "processor reports an editor");
            expect (proc.getName().isNotEmpty(), "processor has a name");
        }

        beginTest ("Silence in -> silence out, across band modes");
        {
            for (int bandMode = 0; bandMode <= 3; ++bandMode)
            {
                ZScorchAudioProcessor proc;
                setParam (proc.getValueTreeState(), "bandCount", (float) bandMode);
                setParam (proc.getValueTreeState(), "lift", 1.0f);

                juce::AudioBuffer<float> silence = makeBuffer (2, 4096);
                auto out = render (proc, silence, 512, 1);

                expect (allFinite (out), "silence stays finite");
                expectLessThan (peak (out), 1.0e-7f);
            }
        }

        beginTest ("Sine wave flows through and stays bounded");
        {
            ZScorchAudioProcessor proc;
            juce::AudioBuffer<float> sine = makeBuffer (2, 8192);
            fillSine (sine, 44100.0, 1000.0f, 0.5f);

            auto out = render (proc, sine, 512, 1);

            expect (allFinite (out), "output finite");
            expectLessThan (peak (out), 1.2f);
            expectGreaterThan (rms (out), 0.05f);
        }

        beginTest ("Editor-facing interfaces return sane values");
        {
            ZScorchAudioProcessor proc;
            proc.prepareToPlay (44100.0, 512);

            juce::AudioBuffer<float> sine = makeBuffer (2, 4096);
            fillSine (sine, 44100.0, 440.0f, 0.5f);
            juce::MidiBuffer midi;
            proc.processBlock (sine, midi);

            expectGreaterThan (proc.getNumBins(), 100);
            expectGreaterThan (proc.getMagnitudeData().size(), (size_t) 100);
            expect (proc.getHostSampleRate() == 44100.0, "host sample rate reported");
            expect (proc.getActiveNumBands() >= 1 && proc.getActiveNumBands() <= 4, "band count sane");
            expect (std::isfinite (proc.getProcessingLoadPercent()), "CPU meter finite");
            expect (proc.getProcessingLoadPercent() >= 0.0f, "CPU meter non-negative");
            expect (proc.getUndoManager() != nullptr, "undo manager available");
            expect (proc.isLicensedForUse(), "trial/licensed state allows use");
        }

        beginTest ("Processing load stays below real time (CPU budget)");
        {
            ZScorchAudioProcessor proc;
            setParam (proc.getValueTreeState(), "lift", 0.8f);
            setParam (proc.getValueTreeState(), "satDriveLow", 0.7f);
            setParam (proc.getValueTreeState(), "exciterEnabled", 1.0f);
            setParam (proc.getValueTreeState(), "exciterAmount", 0.5f);

            const double sr = 48000.0;
            const int blockSize = 512;
            const int seconds = 5;
            const int blocks = (int) (sr * seconds / blockSize);

            proc.prepareToPlay (sr, blockSize);

            juce::AudioBuffer<float> block (2, blockSize);
            fillSine (block, sr, 997.0f, 0.4f);

            juce::MidiBuffer midi;
            const auto start = juce::Time::getHighResolutionTicks();

            for (int b = 0; b < blocks; ++b)
                proc.processBlock (block, midi);

            const auto elapsed = juce::Time::getHighResolutionTicks() - start;
            const double seconds2 = (double) elapsed / juce::Time::getHighResolutionTicksPerSecond();
            const double realtimeFactor = seconds2 / (double) seconds;   // < 1 = faster than real time
            const double cpuPercent = 100.0 * realtimeFactor;            // % of a single core

            std::cout << "      [perf] default (ADAA) path: " << seconds2 << " s for "
                      << seconds << " s audio  ->  ~" << juce::String (cpuPercent, 2)
                      << "% of a single core (RTF " << juce::String (realtimeFactor, 4) << ")"
                      << std::endl;

            expectLessThan (realtimeFactor, 1.0,
                            "must render faster than real time on a desktop CPU");
        }
    }
};

static SmokeTests smokeTests;

void registerSmokeTests() { juce::ignoreUnused (smokeTests); }
