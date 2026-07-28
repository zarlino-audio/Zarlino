//==============================================================================
#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "TestMacros.h"

//==============================================================================
void testSmoke()
{
    bool _testOk;
    TEST("Smoke: processor instantiation")
    {
        ZAdaptiveHarmonicsAudioProcessor processor;
        CHECK(processor.getBusesLayout().getMainInputChannels() > 0);

        const auto& tree = processor.getValueTreeState();
        CHECK(tree.getRawParameterValue ("lift") != nullptr);
        CHECK(tree.getRawParameterValue ("character") != nullptr);
        CHECK(tree.getRawParameterValue ("mix") != nullptr);
        CHECK(tree.getRawParameterValue ("saturatorEven") != nullptr);
        CHECK(tree.getRawParameterValue ("saturatorOdd") != nullptr);
        CHECK(tree.getRawParameterValue ("saturatorHarmReduction") != nullptr);
        CHECK(tree.getRawParameterValue ("tiltAmount") != nullptr);
        CHECK(tree.getRawParameterValue ("exciterAmount") != nullptr);
        CHECK(tree.getRawParameterValue ("exciterEnabled") != nullptr);
    }
    END_TEST()

    TEST("Smoke: prepareToPlay / processBlock / releaseResources")
    {
        std::cerr << "\n    [ctor]"; // debug
        ZAdaptiveHarmonicsAudioProcessor processor;
        std::cerr << "\n    [rate]"; // debug
        processor.setRateAndBufferSizeDetails (48000.0, 256);
        std::cerr << "\n    [prep]"; // debug
        processor.prepareToPlay (48000.0, 256);
        std::cerr << "\n    [buf]"; // debug

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        juce::MidiBuffer midi;

        std::cerr << "\n    [proc1]"; // debug
        processor.processBlock (buffer, midi);
        std::cerr << "\n    [proc2]"; // debug
        processor.processBlock (buffer, midi);
        std::cerr << "\n    [rel]"; // debug
        processor.releaseResources();
        std::cerr << std::endl; // debug
    }
    END_TEST()

    TEST("Smoke: state save/load round-trip")
    {
        ZAdaptiveHarmonicsAudioProcessor processor;

        auto* liftRaw = processor.getValueTreeState().getRawParameterValue ("lift");
        CHECK(liftRaw != nullptr);
        liftRaw->store (0.75f);

        juce::MemoryBlock savedState;
        processor.getStateInformation (savedState);
        CHECK(savedState.getSize() > 0);

        ZAdaptiveHarmonicsAudioProcessor processor2;
        processor2.setStateInformation (savedState.getData(),
                                        static_cast<int> (savedState.getSize()));

        auto* liftRaw2 = processor2.getValueTreeState().getRawParameterValue ("lift");
        CHECK(liftRaw2 != nullptr);
        const float loadedVal = liftRaw2->load();
        CHECK(loadedVal > 0.7f && loadedVal < 0.8f);
    }
    END_TEST()

    TEST("Smoke: editor exists")
    {
        ZAdaptiveHarmonicsAudioProcessor processor;
        CHECK(processor.hasEditor());
    }
    END_TEST()
}
