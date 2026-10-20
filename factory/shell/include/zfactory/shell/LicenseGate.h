#pragma once

#include <string>

namespace zfactory
{
namespace shell
{

//==============================================================================
// Licensing hooks.
//
// Every generated product uses the same licensing implementation and the same
// key material as the hand-written Zarlino plugins: the shell calls
// Zarlino::Licensing::ZLicenseManager with the product's own specification id,
// so issuing a licence for a generated product needs no new code and no new
// key. The shell only ever sees the narrow summary below, which keeps the
// licensing implementation an implementation detail of the product target.
//
// When the product is built without the licensing library (ZFACTORY_HAVE_LICENSING
// undefined) the hooks report an unlicensed trial and the editor says so, rather
// than silently pretending to be licensed.
//==============================================================================

enum class LicenseStateName
{
    Trial,
    TrialExpired,
    Licensed,
    Invalid
};

struct LicenseSummary
{
    LicenseStateName state = LicenseStateName::Invalid;
    int trialDaysRemaining = 0;
    std::string licensee;
    bool unlocked = false;
    std::string pluginId;
    bool licensingCompiledIn = false;

    /** Short text for the editor footer. */
    std::string describe() const;
};

/** Initialises licensing for this product. Safe to call more than once. */
void initializeLicensing (const std::string& pluginId);

/** Current licence state. Cheap; safe from the message thread. */
LicenseSummary currentLicense();

} // namespace shell
} // namespace zfactory
