#include "AIIntegration.h"

AIIntegration::AIIntegration() : juce::Thread ("AIThread")
{
    startThread();
}

AIIntegration::~AIIntegration()
{
    mStopThread = true;
    stopThread (5000);
}

void AIIntegration::initialize (const juce::String& apiKey)
{
    mAPIKey = apiKey;
}

void AIIntegration::sendPrompt (const juce::String& userPrompt,
                                const juce::String& sessionJSON,
                                const std::vector<AnalysisSnapshot>& recentSnapshots)
{
    juce::ScopedLock lock (mChatLock);
    
    ChatMessage userMessage;
    userMessage.role = "user";
    userMessage.content = userPrompt;
    mChatHistory.push_back (userMessage);
}

juce::String AIIntegration::getChatHistory() const
{
    juce::ScopedLock lock (mChatLock);
    juce::String history;
    for (const auto& msg : mChatHistory)
        history += msg.role + ": " + msg.content + "\n";
    return history;
}

std::vector<SuggestionCard> AIIntegration::getPendingSuggestions() const
{
    juce::ScopedLock lock (mSuggestionLock);
    return mPendingSuggestions;
}

void AIIntegration::applySuggestion (int suggestionIndex)
{
    juce::ScopedLock lock (mSuggestionLock);
    if (suggestionIndex >= 0 && suggestionIndex < static_cast<int> (mPendingSuggestions.size()))
    {
        // TODO: Apply the suggestion
        mPendingSuggestions.erase (mPendingSuggestions.begin() + suggestionIndex);
    }
}

void AIIntegration::rejectSuggestion (int suggestionIndex)
{
    juce::ScopedLock lock (mSuggestionLock);
    if (suggestionIndex >= 0 && suggestionIndex < static_cast<int> (mPendingSuggestions.size()))
        mPendingSuggestions.erase (mPendingSuggestions.begin() + suggestionIndex);
}

void AIIntegration::modifySuggestion (int suggestionIndex, const juce::String& newInstruction)
{
    juce::ScopedLock lock (mSuggestionLock);
    if (suggestionIndex >= 0 && suggestionIndex < static_cast<int> (mPendingSuggestions.size()))
    {
        // TODO: Re-query AI with modified instruction
    }
}

void AIIntegration::run()
{
    while (!mStopThread)
    {
        // Background thread work for streaming API calls
        juce::Thread::sleep (100);
    }
}
