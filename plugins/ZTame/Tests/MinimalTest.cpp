#include <JuceHeader.h>
#include <iostream>

// Plugin defines already on command line
#include "PluginProcessor.h"
#include <iostream>

class ProcTest : public juce::UnitTest
{
public:
    ProcTest() : UnitTest("ProcTest") {}
    void runTest() override
    {
        std::cout << "beginTest..." << std::endl;
        beginTest ("create");
        std::cout << "creating proc..." << std::endl;
        auto proc = std::make_unique<ZTameAudioProcessor>();
        std::cout << "prepareToPlay..." << std::endl;
        proc->prepareToPlay(44100.0, 512);
        std::cout << "setting params..." << std::endl;
        auto& vts = proc->getValueTreeState();
        if (auto* p = vts.getParameter("bypass"))
            p->setValueNotifyingHost(p->convertTo0to1(0.0f));
        std::cout << "making buffer..." << std::endl;
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        juce::MidiBuffer midi;
        std::cout << "processBlock..." << std::endl;
        proc->processBlock(buf, midi);
        std::cout << "done!" << std::endl;
        expect(true);
    }
};

static ProcTest pt;

int main() {
    std::cout << "Starting..." << std::endl;
    juce::ScopedJuceInitialiser_GUI init;
    std::cout << "Init done, running..." << std::endl;

    juce::UnitTestRunner runner;
    runner.runAllTests();

    std::cout << "Done!" << std::endl;

    // AudioProcessor destruction can hang console builds (APVTS / message-manager
    // teardown ordering). Skip stack unwinding on exit.
    std::cout.flush();
    std::exit (0);
}
