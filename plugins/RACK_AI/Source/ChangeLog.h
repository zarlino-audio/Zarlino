#pragma once

#include <JuceHeader.h>
#include <vector>

struct ChangeLogEntry
{
    uint64_t timestamp = 0;
    int slotIndex = 0;
    juce::String parameterName;
    float beforeValue = 0.0f;
    float afterValue = 0.0f;
    juce::String mode;  // "Autonomous" or "Suggested"
    juce::String response;  // "Applied", "Rejected", "Modified"
    float confidenceScore = 0.5f;
    juce::String aiExplanation;
};

class ChangeLog
{
public:
    ChangeLog() = default;
    ~ChangeLog() = default;

    void addEntry (const ChangeLogEntry& entry);
    const std::vector<ChangeLogEntry>& getEntries() const { return mEntries; }
    void clear() { mEntries.clear(); }

    // Serialize to JSON for AI context
    juce::String toJSON() const;

    // Find entries by slot
    std::vector<ChangeLogEntry> getEntriesForSlot (int slotIndex) const;

private:
    std::vector<ChangeLogEntry> mEntries;
    juce::CriticalSection mEntriesLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChangeLog)
};
