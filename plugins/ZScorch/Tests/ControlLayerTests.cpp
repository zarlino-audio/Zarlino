//==============================================================================
// ZScorch v2.0 — ControlLayer tests (macro mapping, mode selection, smoothing)
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "ControlLayer.h"
#include "AnalysisCore.h"

using namespace ztest;

namespace
{
    ControlVector busyVector()
    {
        ControlVector cv;
        cv.harmonicDensity  = 0.5f;
        cv.spectralFlatness = 0.3f;
        cv.crestFactor      = 0.4f;
        cv.brightness       = 0.4f;
        cv.activity         = 0.6f;
        return cv;
    }
}

class ControlLayerTests : public juce::UnitTest
{
public:
    ControlLayerTests() : juce::UnitTest ("ZScorch Control Layer") {}

    void runTest() override
    {
        beginTest ("Lift = 0 silences the adaptive chain");
        {
            ControlLayer cl;
            cl.prepare (44100.0);
            cl.update (busyVector(), 0.0f, 0.0f, 0.8f, 0.0f, 0.7f, 0.5f, 0.5f, 1.0f, 0.0f, 44100.0);

            const auto t = cl.getCurrentTargets();
            expectWithinAbsoluteError (t.satDrive, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (t.tiltAmount, 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (t.exciterAmount, 0.0f, 1.0e-6f);
        }

        beginTest ("Drive increases monotonically with Lift");
        {
            float prev = -1.0f;
            for (float lift = 0.0f; lift <= 1.0f; lift += 0.1f)
            {
                ControlLayer cl;
                cl.prepare (44100.0);
                cl.update (busyVector(), lift, 0.0f, 0.8f, 0.0f, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 44100.0);

                const float drive = cl.getNextTarget().satDrive;
                expect (drive >= prev - 1.0e-6f, "drive grows with lift");
                prev = drive;
            }
        }

        beginTest ("Saturation mode parameter maps to the six v2 curves");
        {
            struct Case { float param; SaturationMode mode; };
            const Case cases[] = {
                { 0.00f, SaturationMode::Tube },
                { 0.25f, SaturationMode::Tape },
                { 0.42f, SaturationMode::Transistor },
                { 0.58f, SaturationMode::Germanium },
                { 0.75f, SaturationMode::Diode },
                { 0.92f, SaturationMode::Wavefold },
            };

            for (auto& c : cases)
            {
                ControlLayer cl;
                cl.prepare (44100.0);
                cl.update (busyVector(), 0.8f, 0.0f, 0.8f, c.param, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 44100.0);
                expect (cl.getCurrentTargets().satMode == c.mode,
                        "mode mapping for param " + juce::String (c.param, 2));
            }
        }

        beginTest ("Smoothing advances towards the target and settles");
        {
            ControlLayer cl;
            cl.prepare (44100.0);
            cl.reset();

            cl.update (busyVector(), 1.0f, 0.0f, 0.9f, 0.0f, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 44100.0);

            float prev = cl.getCurrentTargets().satDrive;
            expectWithinAbsoluteError (prev, 0.0f, 1.0e-6f);

            for (int i = 0; i < 200; ++i)   // 200 blocks of 512 samples >> 50 ms ramp
            {
                cl.skipSamples (512);
                const float now = cl.getCurrentTargets().satDrive;
                expect (now >= prev - 1.0e-6f, "monotone ramp");
                expect (std::isfinite (now), "finite ramp");
                prev = now;
            }

            expectGreaterThan (prev, 0.0f, "target reached");
        }

        beginTest ("Character direction sets tilt sign");
        {
            ControlLayer up, down;
            up.prepare (44100.0);
            down.prepare (44100.0);

            up.update (busyVector(), 1.0f, 1.0f, 0.5f, 0.0f, 0.7f, 1.0f, 0.0f, 0.0f, 0.0f, 44100.0);
            down.update (busyVector(), 1.0f, -1.0f, 0.5f, 0.0f, 0.7f, 1.0f, 0.0f, 0.0f, 0.0f, 44100.0);

            const auto tUp = up.getNextTarget();
            const auto tDown = down.getNextTarget();

            expectGreaterThan (tUp.tiltAmount, tDown.tiltAmount - 1.0e-6f);
            expect (std::isfinite (tUp.tiltAmount) && std::isfinite (tDown.tiltAmount), "finite tilt");
        }

        beginTest ("Exciter stays gated unless enabled");
        {
            ControlLayer cl;
            cl.prepare (44100.0);

            cl.update (busyVector(), 1.0f, 1.0f, 0.5f, 0.0f, 0.7f, 0.5f, 1.0f, 0.0f, 0.0f, 44100.0);
            expectWithinAbsoluteError (cl.getCurrentTargets().exciterEnabled, 0.0f, 1.0e-6f);

            cl.update (busyVector(), 1.0f, 1.0f, 0.5f, 0.0f, 0.7f, 0.5f, 1.0f, 1.0f, 0.0f, 44100.0);
            expectGreaterThan (cl.getCurrentTargets().exciterEnabled, 0.5f);
        }

        beginTest ("Band gain passthrough");
        {
            ControlLayer cl;
            cl.prepare (44100.0);
            cl.update (busyVector(), 1.0f, 0.0f, 0.5f, 0.0f, 0.7f, 0.5f, 0.0f, 0.0f, 3.5f, 44100.0);
            expectWithinAbsoluteError (cl.getCurrentTargets().bandGainDb, 3.5f, 1.0e-5f);
        }

        beginTest ("Extreme control vectors do not produce NaN targets");
        {
            ControlVector wild;
            wild.harmonicDensity  = -5.0f;
            wild.spectralFlatness = 12.0f;
            wild.crestFactor      = -1.0f;
            wild.brightness       = 3.0f;
            wild.activity         = -0.5f;

            ControlLayer cl;
            cl.prepare (48000.0);
            cl.update (wild, 1.0f, -1.0f, 1.0f, 2.0f, 5.0f, 1.0f, 1.0f, 1.0f, 99.0f, 48000.0);

            const auto t = cl.getNextTarget();
            expect (std::isfinite (t.satDrive) && std::isfinite (t.tiltAmount)
                    && std::isfinite (t.exciterAmount) && std::isfinite (t.bandGainDb),
                    "all targets finite under abuse");
        }
    }
};

static ControlLayerTests controlLayerTests;

void registerControlLayerTests() { juce::ignoreUnused (controlLayerTests); }
