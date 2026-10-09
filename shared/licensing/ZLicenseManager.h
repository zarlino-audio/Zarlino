#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#include "ZLicenseStatus.h"
#include "ZLicenseKey.h"

#ifdef ZARLINO_HAVE_PUBLIC_KEY
#include "ZarlinoPublicKey.h"
#endif

namespace Zarlino::Licensing {

class ZLicenseManager {
public:
    struct LicenseStatusInfo {
        LicenseState state = LicenseState::Invalid;
        LicenseInfo info;
        int daysRemaining = 0;
    };

    static ZLicenseManager& getInstance()
    {
        static ZLicenseManager instance;
        return instance;
    }

    // Production public key is embedded via CMake (see shared/licensing/CMakeLists.txt):
    //   -DZARLINO_LICENSE_PUBLIC_KEY_FILE=path/to/zarlino_license_public.txt
    // or the ZARLINO_LICENSE_PUBLIC_KEY environment variable (CI). Both are a single
    // line "<modulus_hex>,<exponent_hex>". The generated ZarlinoPublicKey.h defines
    // ZARLINO_LICENSE_PUBLIC_KEY_MODULUS_HEX / ZARLINO_LICENSE_PUBLIC_KEY_EXPONENT_HEX,
    // which are returned here.
    //
    // When no key is configured these return empty strings and verification is
    // disabled (trial-only mode — NOT FOR PRODUCTION).
    static juce::String getDefaultModulusHex()
    {
#ifdef ZARLINO_LICENSE_PUBLIC_KEY_MODULUS_HEX
        return ZARLINO_LICENSE_PUBLIC_KEY_MODULUS_HEX;
#else
        return {};
#endif
    }

    static juce::String getDefaultExponentHex()
    {
#ifdef ZARLINO_LICENSE_PUBLIC_KEY_EXPONENT_HEX
        return ZARLINO_LICENSE_PUBLIC_KEY_EXPONENT_HEX;
#else
        return {};
#endif
    }

    void initialize(const juce::String& pluginId,
                    const juce::String& modulusHex = getDefaultModulusHex(),
                    const juce::String& exponentHex = getDefaultExponentHex())
    {
        std::lock_guard<std::recursive_mutex> lock(fileMutex_);
        pluginId_ = pluginId;
        modulusHex_ = modulusHex;
        exponentHex_ = exponentHex;
        loadLicenseFromDisk();
        startTrialIfNeeded();
        updateStatusLocked();
    }

    LicenseState getStatus() const noexcept { return static_cast<LicenseState>(status_.load()); }
    bool isUnlocked() const noexcept { return unlocked_.load(); }

    LicenseStatusInfo getStatusInfo() const
    {
        LicenseStatusInfo info;
        info.state = getStatus();
        info.info = currentLicense_.info;
        info.daysRemaining = getTrialDaysRemaining();
        return info;
    }

    int getTrialDaysRemaining() const
    {
        auto start = trialStartTime_;
        if (start == juce::Time()) return trialDays_;
        int64_t ms = juce::Time::getCurrentTime().toMilliseconds() - start.toMilliseconds();
        int remaining = trialDays_ - (int)(ms / (1000 * 60 * 60 * 24));
        return remaining < 0 ? 0 : remaining;
    }

    juce::String getLicenseeEmail() const
    {
        std::lock_guard<std::recursive_mutex> lock(fileMutex_);
        return currentLicense_.info.licenseeEmail;
    }

    bool applyLicenseKey(const juce::String& key)
    {
        std::lock_guard<std::recursive_mutex> lock(fileMutex_);
        ZLicenseKey::Parsed parsed;
        if (!ZLicenseKey::parseLicenseKey(key, parsed))
            return false;
        juce::String err;
        if (!verifyParsedLicense(parsed, err))
            return false;
        // check plugin id
        if (parsed.info.pluginId != pluginId_)
            return false;
        currentLicense_ = parsed;
        unlocked_.store(true);
        status_.store(static_cast<int>(LicenseState::Licensed));
        // persist
        juce::PropertiesFile::Options opts = getPropertiesOptions();
        juce::PropertiesFile props(opts);
        props.setValue("licenseKey", key);
        props.setValue("trialStart", juce::String(trialStartTime_.toMilliseconds()));
        props.saveIfNeeded();
        return true;
    }

    bool applyLicenseFile(const juce::File& file)
    {
        if (!file.existsAsFile()) return false;
        juce::String txt = file.loadFileAsString();
        if (txt.isEmpty()) return false;
        // Trim and try as base64
        return applyLicenseKey(txt.trim());
    }

    void saveLicenseToDisk()
    {
        std::lock_guard<std::recursive_mutex> lock(fileMutex_);
        juce::PropertiesFile::Options opts = getPropertiesOptions();
        juce::PropertiesFile props(opts);
        if (currentLicense_.rawPayload.getSize() > 0)
        {
            juce::MemoryOutputStream mos;
            mos.write(currentLicense_.rawPayload.getData(), (int)currentLicense_.rawPayload.getSize());
            uint16_t sigLen = (uint16_t)currentLicense_.signature.getSize();
            mos.writeShort((int)sigLen);
            mos.write(currentLicense_.signature.getData(), (int)currentLicense_.signature.getSize());
            juce::String b64 = juce::Base64::toBase64(mos.getMemoryBlock().getData(), (int)mos.getMemoryBlock().getSize());
            props.setValue("licenseKey", b64);
        }
        props.setValue("trialStart", juce::String(trialStartTime_.toMilliseconds()));
        props.saveIfNeeded();
    }

    void loadLicenseFromDisk()
    {
        std::lock_guard<std::recursive_mutex> lock(fileMutex_);
        juce::PropertiesFile::Options opts = getPropertiesOptions();
        juce::PropertiesFile props(opts);
        juce::String key = props.getValue("licenseKey", {});
        juce::String ts = props.getValue("trialStart", {});
        if (!ts.isEmpty())
        {
            int64_t ms = ts.getLargeIntValue();
            if (ms > 0)
                trialStartTime_ = juce::Time(ms);
        }
        if (!key.isEmpty())
        {
            ZLicenseKey::Parsed parsed;
            if (ZLicenseKey::parseLicenseKey(key, parsed))
            {
                juce::String err;
                if (verifyParsedLicense(parsed, err) && parsed.info.pluginId == pluginId_)
                {
                    currentLicense_ = parsed;
                    unlocked_.store(true);
                    status_.store(static_cast<int>(LicenseState::Licensed));
                    return;
                }
            }
        }
        // fallback: trial mode
        unlocked_.store(false);
        status_.store(static_cast<int>(LicenseState::Trial));
    }

private:
    ZLicenseManager() = default;
    ~ZLicenseManager() = default;

    void startTrialIfNeeded()
    {
        if (currentLicense_.info.pluginId.isNotEmpty()) return;
        if (trialStartTime_ != juce::Time()) return;
        trialStartTime_ = juce::Time::getCurrentTime();
        juce::PropertiesFile::Options opts = getPropertiesOptions();
        juce::PropertiesFile props(opts);
        props.setValue("trialStart", juce::String(trialStartTime_.toMilliseconds()));
        props.saveIfNeeded();
    }

    void updateStatusLocked()
    {
        if (unlocked_.load()) { status_.store(static_cast<int>(LicenseState::Licensed)); return; }
        if (trialStartTime_ == juce::Time()) { status_.store(static_cast<int>(LicenseState::Trial)); return; }
        int remaining = getTrialDaysRemaining();
        if (remaining <= 0) status_.store(static_cast<int>(LicenseState::TrialExpired));
        else status_.store(static_cast<int>(LicenseState::Trial));
    }

    juce::PropertiesFile::Options getPropertiesOptions() const
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "ZarlinoAudio";
        opts.folderName = "zarlino_audio";
        opts.filenameSuffix = "license";
        opts.commonToAllUsers = false;
        return opts;
    }

    bool verifyParsedLicense(const ZLicenseKey::Parsed& parsed, juce::String& outError) const
    {
        const juce::String mod = modulusHex_.isEmpty() ? getDefaultModulusHex() : modulusHex_;
        const juce::String exp = exponentHex_.isEmpty() ? getDefaultExponentHex() : exponentHex_;
        return ZLicenseKey::verifySignature(parsed, mod, exp, outError);
    }

    // members
    juce::String pluginId_;
    juce::String modulusHex_;
    juce::String exponentHex_;

    std::atomic<int> status_{static_cast<int>(LicenseState::Invalid)};
    std::atomic<bool> unlocked_{false};

    mutable std::recursive_mutex fileMutex_;
    ZLicenseKey::Parsed currentLicense_;
    juce::Time trialStartTime_;

    static constexpr int trialDays_ = 14;
};

} // namespace Zarlino::Licensing
