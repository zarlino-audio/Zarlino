//==============================================================================
// ZScorch v2.0 — licensing: trial state, invalid keys, processor gating.
//==============================================================================
#include <JuceHeader.h>
#include "TestUtils.h"
#include "PluginProcessor.h"
#include "shared/licensing/ZLicenseManager.h"

using namespace ztest;
using Zarlino::Licensing::LicenseState;
using Zarlino::Licensing::ZLicenseManager;

class LicenseTests : public juce::UnitTest
{
public:
    LicenseTests() : juce::UnitTest ("ZScorch Licensing") {}

    void runTest() override
    {
        beginTest ("Manager initialises into a usable trial or licensed state");
        {
            auto& mgr = ZLicenseManager::getInstance();
            mgr.initialize ("ZSCORCH");

            const auto state = mgr.getStatus();
            expect (state == LicenseState::Trial || state == LicenseState::Licensed
                    || state == LicenseState::TrialExpired,
                    "known state: " + juce::String ((int) state));

            if (state == LicenseState::Trial)
            {
                const int days = mgr.getTrialDaysRemaining();
                expect (days >= 0 && days <= 31, "trial days sane: " + juce::String (days));
            }
        }

        beginTest ("Invalid license keys are rejected and change nothing");
        {
            auto& mgr = ZLicenseManager::getInstance();
            mgr.initialize ("ZSCORCH");

            const auto before = mgr.getStatus();

            expect (! mgr.applyLicenseKey ({}), "empty key rejected");
            expect (! mgr.applyLicenseKey ("not-a-license"), "garbage rejected");
            expect (! mgr.applyLicenseKey (juce::String::repeatedString ("A", 512)),
                    "oversized garbage rejected");

            expect (mgr.getStatus() == before, "state unchanged after rejected keys");
        }

        beginTest ("Processor gates on the licensing state consistently");
        {
            ZScorchAudioProcessor proc;
            auto& mgr = ZLicenseManager::getInstance();
            const auto state = mgr.getStatus();

            const bool expected = (state == LicenseState::Licensed || state == LicenseState::Trial);
            expect (proc.isLicensedForUse() == expected,
                    "isLicensedForUse matches manager state");

            // The processor never blocks audio: an unlicensed state must still
            // process (enforcement is a UI concern, never a drop-out).
            proc.prepareToPlay (44100.0, 512);
            juce::AudioBuffer<float> block (2, 512);
            fillSine (block, 44100.0, 440.0f, 0.5f);
            juce::MidiBuffer midi;
            proc.processBlock (block, midi);
            expect (allFinite (block), "audio always processed");
        }

        beginTest ("License manager is usable from the editor path");
        {
            ZScorchAudioProcessor proc;
            auto& mgr = proc.getLicenseManager();
            expect (&mgr == &ZLicenseManager::getInstance(), "singleton access");

            const auto info = mgr.getStatusInfo();
            expect (info.state == mgr.getStatus(), "status info matches status");
        }
    }
};

static LicenseTests licenseTests;

void registerLicenseTests() { juce::ignoreUnused (licenseTests); }
