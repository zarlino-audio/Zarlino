#pragma once

#include <JuceHeader.h>
#include <array>
#include <deque>
#include <memory>
#include "AudioAnalysisEngine.h"
#include "ParameterControl.h"
#include "AIIntegration.h"
#include "ChangeLog.h"

// Consolidated RackAIAudioProcessor — combines the working RackAIAudioProcessor
// functionality with the analysis/AI members from the newer RACK_AIAudioProcessor.
class RackAIAudioProcessor : public juce::AudioProcessor
{
public:
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

   // ---- Types ----
   enum class Routing { series, busA, busB, busC, busD };
   enum class AiMode { suggest, autonomous };

   struct AnalysisSnapshot
   {
       float rmsDb = -100.0f;
       float peakDb = -100.0f;
       float crestDb = 0.0f;
       float stereoWidth = 0.0f;
   };

   struct DecisionLogEntry
   {
       juce::String time;
       juce::String slot;
       juce::String parameter;
       juce::String before;
       juce::String after;
       juce::String mode;
       juce::String response;
   };

   struct SlotState
   {
       juce::String name;
       Routing routing = Routing::series;
       float blend = 1.0f;
       bool bypassed = false;
       bool solo = false;
       float preGainDb = 0.0f;
       float postGainDb = 0.0f;
   };

   // ---- Public API used by the editor ----
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

   // Additional features merged from the newer class
   void sendPromptToAI (const juce::String& userPrompt);
   juce::String getAIChatHistory() const;

private:
   void seedDefaultSession();
   juce::ValueTree createStateTree() const;
   void restoreFromStateTree (const juce::ValueTree& tree);
   void appendDecision (const DecisionLogEntry& entry);

   static juce::String nowForLog();
   static float gainFromDb (float db) { return juce::Decibels::decibelsToGain (db); }

   // Core session state
   juce::Array<SlotState> slots;
   juce::StringArray chatHistory;
   juce::Array<DecisionLogEntry> decisionLog;
   juce::String sessionIntent;
   AiMode aiMode = AiMode::suggest;

   // Analysis meters
   std::atomic<float> rmsDb { -100.0f };
   std::atomic<float> peakDb { -100.0f };
   std::atomic<float> crestDb { 0.0f };
   std::atomic<float> stereoWidth { 0.0f };

   double currentSampleRate = 44100.0;
   int currentBlockSize = 512;

   // Merged-in components from newer class (non-owning — used for analysis/AI)
   std::unique_ptr<AudioAnalysisEngine> mAnalysisEngine;
   std::unique_ptr<ParameterControl> mParameterControl;
   std::unique_ptr<AIIntegration> mAIIntegration;
   ChangeLog mChangeLog;

   // Lock used for non-real-time editor/state operations. processBlock must NOT hold this while calling other plugins.
   juce::CriticalSection mSlotLock;

   JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackAIAudioProcessor)
};
