#pragma once

#include <JuceHeader.h>
#include <array>
#include <deque>
#include <memory>
#include "AudioAnalysisEngine.h"
#include "ParameterControl.h"
#include "AIIntegration.h"
#include "ChangeLog.h"

class RACK_AIAudioProcessor : public juce::AudioProcessor
{
public:
    RACK_AIAudioProcessor();
    ~RACK_AIAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RACK/AI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // RACK/AI specific methods
    int getNumSlots() const { return static_cast<int> (mSlotConfigs.size()); }
    void addSlot();
    void removeSlot (int slotIndex);
    void setSlotBypass (int slotIndex, bool isBypassed);
    void setSlotMix (int slotIndex, float wetDryMix);
    void setSlotRouting (int slotIndex, juce::String routing); // "Series" or "ParallelA/B/C/D"

    // Analysis access
    const AnalysisSnapshot& getLatestAnalysis (int slotIndex) const;
    std::vector<AnalysisSnapshot> getAnalysisHistory() const;

    // AI communication
    void sendPromptToAI (const juce::String& userPrompt);
    juce::String getAIChatHistory() const;
    void setAIMode (AIIntegration::Mode mode);
    AIIntegration::Mode getAIMode() const;

    // Change log access
    const std::vector<ChangeLogEntry>& getChangeLog() const { return mChangeLog.getEntries(); }
    void applyChangeLogEntry (const ChangeLogEntry& entry);

private:
    struct SlotConfig
    {
        juce::String pluginName;
        bool isBypassed = false;
        float wetDryMix = 1.0f;
        juce::String routing = "Series"; // Series or ParallelX
        std::unique_ptr<juce::AudioPluginInstance> pluginInstance;
        AnalysisSnapshot preAnalysis;
        AnalysisSnapshot postAnalysis;
    };

    std::array<SlotConfig, 16> mSlotConfigs;
    int mNumActiveSlots = 0;
    double mSampleRate = 44100.0;
    int mBlockSize = 512;

    std::unique_ptr<AudioAnalysisEngine> mAnalysisEngine;
    std::unique_ptr<ParameterControl> mParameterControl;
    std::unique_ptr<AIIntegration> mAIIntegration;
    ChangeLog mChangeLog;

    juce::CriticalSection mSlotLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RACK_AIAudioProcessor)
};
        juce::String mode;
        juce::String response;
    };

    RackAIAudioProcessor();
    ~RackAIAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static constexpr int maxSlots = 16;

    int getSlotCount() const;
    const SlotState& getSlot (int index) const;
    AnalysisSnapshot getAnalysisSnapshot() const;
    juce::StringArray getChatHistory() const;
    juce::Array<DecisionLogEntry> getDecisionLog() const;

    AiMode getAiMode() const;
    void setAiMode (AiMode newMode);
    juce::String getSessionIntent() const;
    void setSessionIntent (const juce::String& intent);

    bool addPlaceholderSlot();
    void removeSlot (int index);
    void moveSlot (int from, int to);
    void setSlotBypassed (int index, bool shouldBypass);
    void setSlotSolo (int index, bool shouldSolo);
    void setSlotRouting (int index, Routing routing);
    void setSlotBlend (int index, float blend);
    void submitPrompt (const juce::String& prompt);

    static juce::String routingToString (Routing routing);
    static Routing routingFromString (const juce::String& routing);
    static juce::String aiModeToString (AiMode mode);

private:
    void seedDefaultSession();
    juce::ValueTree createStateTree() const;
    void restoreFromStateTree (const juce::ValueTree& tree);
    void appendDecision (const DecisionLogEntry& entry);

    juce::Array<SlotState> slots;
    juce::StringArray chatHistory;
    juce::Array<DecisionLogEntry> decisionLog;
    juce::String sessionIntent;
    AiMode aiMode = AiMode::suggest;

    std::atomic<float> rmsDb { -100.0f };
    std::atomic<float> peakDb { -100.0f };
    std::atomic<float> crestDb { 0.0f };
    std::atomic<float> stereoWidth { 0.0f };

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAIAudioProcessor)
};
