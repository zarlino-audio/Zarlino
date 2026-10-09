#pragma once

#include <JuceHeader.h>

namespace Zarlino::Licensing {

enum class LicenseState {
    Trial,
    TrialExpired,
    Licensed,
    Invalid
};

struct LicenseInfo {
    juce::String pluginId;
    juce::String licenseeEmail;
    juce::String issueDate;      // ISO yyyy-mm-dd
    juce::String expirationDate; // ISO yyyy-mm-dd or "perpetual"
};

} // namespace Zarlino::Licensing
