#include "zfactory/shell/LicenseGate.h"

#include <atomic>
#include <mutex>

#if ZFACTORY_HAVE_LICENSING
#include <JuceHeader.h>

#include "ZLicenseManager.h"
#endif

namespace zfactory
{
namespace shell
{

namespace
{

std::mutex& licenseMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::string& licensePluginId()
{
    static std::string pluginId;
    return pluginId;
}

std::atomic<bool> g_initialised { false };

} // namespace

//==============================================================================
std::string LicenseSummary::describe() const
{
    if (! licensingCompiledIn)
        return "Licensing library not linked in this build";

    switch (state)
    {
        case LicenseStateName::Licensed:
            return licensee.empty() ? std::string ("Licensed") : ("Licensed to " + licensee);

        case LicenseStateName::Trial:
            return "Trial - " + std::to_string (trialDaysRemaining) + " days remaining";

        case LicenseStateName::TrialExpired:
            return "Trial expired - enter a licence key";

        case LicenseStateName::Invalid:
        default:
            return "Unlicensed";
    }
}

//==============================================================================
void initializeLicensing (const std::string& pluginId)
{
    std::lock_guard<std::mutex> lock (licenseMutex());
    licensePluginId() = pluginId;

#if ZFACTORY_HAVE_LICENSING
    // The generated product passes its own specification id, so the licence file
    // location and the signed payload are keyed by the product - exactly as the
    // hand-written plugins do.
    Zarlino::Licensing::ZLicenseManager::getInstance().initialize (
        juce::String::fromUTF8 (pluginId.c_str()));
#else
    juce::ignoreUnused (pluginId);
#endif

    g_initialised.store (true);
}

LicenseSummary currentLicense()
{
    LicenseSummary summary;

    {
        std::lock_guard<std::mutex> lock (licenseMutex());
        summary.pluginId = licensePluginId();
    }

#if ZFACTORY_HAVE_LICENSING
    summary.licensingCompiledIn = true;

    const auto info = Zarlino::Licensing::ZLicenseManager::getInstance().getStatusInfo();

    switch (info.state)
    {
        case Zarlino::Licensing::LicenseState::Licensed:     summary.state = LicenseStateName::Licensed; break;
        case Zarlino::Licensing::LicenseState::Trial:        summary.state = LicenseStateName::Trial; break;
        case Zarlino::Licensing::LicenseState::TrialExpired: summary.state = LicenseStateName::TrialExpired; break;
        case Zarlino::Licensing::LicenseState::Invalid:
        default:                                             summary.state = LicenseStateName::Invalid; break;
    }

    summary.trialDaysRemaining = info.daysRemaining;
    summary.licensee = info.info.licenseeEmail.toStdString();
    summary.unlocked = Zarlino::Licensing::ZLicenseManager::getInstance().isUnlocked();
#else
    summary.licensingCompiledIn = false;
    summary.state = LicenseStateName::Trial;
    summary.trialDaysRemaining = 0;
    summary.unlocked = false;
#endif

    return summary;
}

} // namespace shell
} // namespace zfactory
