#pragma once

#include <JuceHeader.h>
#include "AnalysisCore.h"
#include "ControlLayer.h"
#include "Saturator.h"
#include "Tilt.h"
#include "Exciter.h"

//==============================================================================
/**
    ZAdaptiveHarmonics — Adaptive harmonic processor that continuously steers
    audio toward warmth, clarity, and energy.

    v1 signal chain: Shared Analysis Core → Saturator → Dynamic Spectral Tilt →
    Exciter (disabled by default), behind a single Lift macro knob.
*/
class ZAdaptiveHarmonicsAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    ZAdaptiveHarmonicsAudioProcessor();
    ~ZAdaptiveHarmonicsAudioProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override               { return true; }

    //==============================================================================
    const juce::String getName() const override   { return JucePlugin_Name; }
    bool acceptsMidi() const override             { return false; }
    bool producesMidi() const override            { return false; }
    bool isMidiEffect() const override            { return false; }
    double getTailLengthSeconds() const override  { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                                    { return 1; }
    int getCurrentProgram() override                                 { return 0; }
    void setCurrentProgram (int) override                            {}
    const juce::String getProgramName (int) override                 { return {}; }
    void changeProgramName (int, const juce::String&) override       {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState()          { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    // Cached parameter pointers for audio-thread access
    std::atomic<float>* liftParam                = nullptr;
    std::atomic<float>* characterParam           = nullptr;
    std::atomic<float>* mixParam                 = nullptr;
    std::atomic<float>* saturatorEvenParam       = nullptr;
    std::atomic<float>* saturatorOddParam        = nullptr;
    std::atomic<float>* saturatorHarmReductionParam = nullptr;
    std::atomic<float>* tiltAmountParam          = nullptr;
    std::atomic<float>* exciterAmountParam       = nullptr;
    std::atomic<float>* exciterEnabledParam      = nullptr;

    // Metering: updated in processBlock, read by UI timer (atomic)
    std::atomic<float> meterLevel { 0.0f };

private:
    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    double currentSampleRate    = 44100.0;
    int    currentSamplesPerBlock = 512;

    // DSP modules
    AnalysisCore analysisCore;
    ControlLayer  controlLayer;
    Saturator     saturator;
    Tilt          tilt;
    Exciter       exciter;

    // Dry buffer for wet/dry mix
    juce::AudioBuffer<float> dryBuffer;

    // Control-rate counter (update analysis every kControlInterval samples)
    static constexpr int kControlInterval = 256;
    int controlCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZAdaptiveHarmonicsAudioProcessor)
};
