/*
    ZTame UI Shot — renders the real editor to a PNG so the layout can be verified
    offscreen (no host, no window manager interaction).

    The processor is fed a few seconds of synthetic resonant material first, so the
    spectrum display, resonance markers and GR meter all carry real data.

    Usage: ZTame_UiShot [output.png]
*/

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iostream>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    void setParam (ZTameAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getValueTreeState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Feed a musical, resonant signal so the visualisation has something to show. */
    void renderDemoAudio (ZTameAudioProcessor& proc, double seconds)
    {
        const int numSamples = (int) (kSampleRate * seconds);

        juce::AudioBuffer<float> block (2, kBlockSize);
        juce::Random rng (5);
        int written = 0;

        juce::MidiBuffer midi;

        while (written < numSamples)
        {
            const int n = juce::jmin (kBlockSize, numSamples - written);

            for (int s = 0; s < n; ++s)
            {
                const int index = written + s;
                const double t = (double) index / kSampleRate;

                // chord bed
                const double bed = 0.18 * (std::sin (2.0 * juce::MathConstants<double>::pi * 262.0 * t)
                                         + std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t));
                // narrow resonances: 1.1 kHz (a body mode) and 5.6 kHz (harshness)
                const double ring1 = 0.22 * std::sin (2.0 * juce::MathConstants<double>::pi * 1100.0 * t)
                                   * (0.6 + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.5 * t));
                const double ring2 = 0.16 * std::sin (2.0 * juce::MathConstants<double>::pi * 5600.0 * t)
                                   * (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 0.33 * t));
                const double noise = 0.01 * (rng.nextDouble() * 2.0 - 1.0);

                const auto v = (float) (bed + ring1 + ring2 + noise);
                block.setSample (0, s, v);
                block.setSample (1, s, v);
            }

            proc.processBlock (block, midi);
            written += n;
        }
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::File outFile = (argc > 1)
        ? juce::File (juce::String (argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ztame-ui.png");

    auto* proc = new ZTameAudioProcessor();
    proc->prepareToPlay (kSampleRate, kBlockSize);

    // Highlight the focus band so the draggable region is visible in the shot.
    setParam (*proc, "focusLow", 700.0f);
    setParam (*proc, "focusHigh", 9000.0f);
    proc->setUiActive (true);

    renderDemoAudio (*proc, 3.0);

    auto* editor = proc->createEditor();
    editor->setSize (ztame::designWidth, ztame::designHeight);

    // The editor refreshes its telemetry at paint time, so no message loop is needed
    // here: createComponentSnapshot() drives paint() and the view comes out live.
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);

    if (image.isNull())
    {
        std::cerr << "Snapshot failed" << std::endl;
        std::exit (2);
    }

    outFile.deleteFile();
    juce::FileOutputStream stream (outFile);

    juce::PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);

    std::cout << (ok ? "Wrote " : "FAILED to write ") << outFile.getFullPathName()
              << "  (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;

    std::cout.flush();
    std::exit (ok ? 0 : 3);
}
