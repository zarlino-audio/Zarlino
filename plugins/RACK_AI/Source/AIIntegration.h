#pragma once

#include <JuceHeader.h>
#include <deque>
#include <memory>
#include "AudioAnalysisEngine.h"

struct SuggestionCard
{
    int slotIndex = 0;
    juce::String parameterName;
    float currentValue = 0.0f;
    float proposedValue = 0.0f;
    juce::String reasoning;
    float confidence = 0.5f;
};

class AIIntegration : public juce::Thread
{
public:
    enum class Mode { Autonomous, Suggest };

    AIIntegration();
    ~AIIntegration() override;

    void initialize (const juce::String& apiKey);
    void setMode (Mode mode) { mMode = mode; }
    Mode getMode() const { return mMode; }

    // Send a prompt to the AI (runs on background thread)
    void sendPrompt (const juce::String& userPrompt,
                     const juce::String& sessionJSON,
                     const std::vector<AnalysisSnapshot>& recentSnapshots);

    // Get the chat history
    juce::String getChatHistory() const;

    // Get suggestions (in Suggest mode)
    std::vector<SuggestionCard> getPendingSuggestions() const;

    // Process AI response (called by UI)
    void applySuggestion (int suggestionIndex);
    void rejectSuggestion (int suggestionIndex);
    void modifySuggestion (int suggestionIndex, const juce::String& newInstruction);

    void run() override;  // Background thread for streaming API calls

    // Callback for when AI response is complete
    std::function<void()> onAIResponseComplete;

private:
    Mode mMode = Mode::Autonomous;
    // IMPORTANT: In production, use juce::Keychain or platform-specific secure storage
    // instead of keeping API keys in plain-text member variables.
    juce::String mAPIKey;
    // NOTE: Make base URL user-configurable in production instead of hardcoding.
    juce::String mOpenRouterBaseURL = "https://openrouter.io/api/v1";
    // NOTE: Make default model configurable by the user in production.
    juce::String mDefaultModel = "anthropic/claude-3.5-sonnet";

    struct ChatMessage
    {
        juce::String role;  // "user" or "assistant"
        juce::String content;
    };

    std::deque<ChatMessage> mChatHistory;
    std::vector<SuggestionCard> mPendingSuggestions;
    bool mStopThread = false;

    juce::CriticalSection mChatLock;
    juce::CriticalSection mSuggestionLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIIntegration)
};
