#include "ChangeLog.h"

void ChangeLog::addEntry (const ChangeLogEntry& entry)
{
    juce::ScopedLock lock (mEntriesLock);
    mEntries.push_back (entry);
}

juce::String ChangeLog::toJSON() const
{
    juce::ScopedLock lock (mEntriesLock);
    juce::String json = "[";
    
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        if (i > 0) json += ",";
        const auto& entry = mEntries[i];
        json += "{\"timestamp\":" + juce::String (entry.timestamp);
        json += ",\"slotIndex\":" + juce::String (entry.slotIndex);
        json += ",\"parameterName\":\"" + entry.parameterName + "\"";
        json += ",\"beforeValue\":" + juce::String (entry.beforeValue);
        json += ",\"afterValue\":" + juce::String (entry.afterValue);
        json += ",\"mode\":\"" + entry.mode + "\"";
        json += ",\"response\":\"" + entry.response + "\"}";
    }
    
    json += "]";
    return json;
}

std::vector<ChangeLogEntry> ChangeLog::getEntriesForSlot (int slotIndex) const
{
    juce::ScopedLock lock (mEntriesLock);
    std::vector<ChangeLogEntry> result;
    
    for (const auto& entry : mEntries)
        if (entry.slotIndex == slotIndex)
            result.push_back (entry);
    
    return result;
}
