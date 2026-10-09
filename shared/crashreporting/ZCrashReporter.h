#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>
#include <string>

// ZCrashReporter: Lightweight crash reporting abstraction for Zarlino plugins.
// Provides a no-op implementation for development/testing and is designed to
// be backed by Sentry Native or Google Breakpad in production builds.
//
// Design decisions:
// - Uses pimpl (pointer-to-implementation) so the header avoids exposing
//   Sentry/Breakpad headers to every plugin that includes this file.
// - Crash handler installation is scoped to the reporter's lifetime, following
//   RAII — constructor installs, destructor removes.
// - Breadcrumbs are queued on the message thread and flushed before each
//   audio block (via processBlockBreadcrumb hook).
//
// Usage:
//   // In AudioProcessor constructor:
//   crashReporter = std::make_unique<ZCrashReporter>();
//   crashReporter->initialize("com.zarlino.ZTame", "1.0.0");
//
//   // In processBlock:
//   crashReporter->processBlockBreadcrumb();
//
// Production build (CMake):
//   target_compile_definitions(MyPlugin PRIVATE ZARLINO_CRASH_HANDLER_SENTRY)
//   target_link_libraries(MyPlugin PRIVATE sentry-native::sentry)

// ============================================================================
// ZCrashReportDetails: Metadata attached to every crash report
// ============================================================================

struct ZCrashReportDetails
{
    std::string pluginId;        // e.g. "com.zarlino.ZTame"
    std::string pluginVersion;   // e.g. "1.0.0"
    std::string osName;          // e.g. "Windows 11", "macOS 14.0"
    std::string hostName;        // e.g. "REAPER v7.01", "Unknown"
    std::string cpuInfo;         // e.g. "Intel Core i7-13700K"
    int totalRamMb = 0;
    int sampleRate = 44100;
    int bufferSize = 512;

    /** Build a human-readable release identifier for Sentry. */
    std::string releaseString() const
    {
        return pluginId + "@" + pluginVersion;
    }

    /** Build environment tag from OS + host + CPU info. */
    std::string environmentTag() const
    {
        return osName + " | " + hostName;
    }
};

// ============================================================================
// ZCrashReporter — Abstract interface
// ============================================================================

class ZCrashReporter
{
public:
    ZCrashReporter() = default;
    virtual ~ZCrashReporter() = default;

    // ---- Lifecycle ----

    /** Initialize the crash handler. Must be called before any breadcrumbs.
     *  @param details  Plugin and system metadata for crash reports
     *  @param dsn      Sentry DSN endpoint URL. If empty, uses no-op mode. */
    virtual void initialize(const ZCrashReportDetails& details,
                            const std::string& dsn = "") = 0;

    /** Shut down crash handler. Blocks until pending reports are flushed.
     *  Safe to call from destructor. */
    virtual void shutdown() = 0;

    // ---- Breadcrumbs ----

    /** Add a breadcrumb — a diagnostic event logged before a crash.
     *  Thread-safe. Use this for:
     *  - "User changed parameter [gain] to -6.0 dB"
     *  - "Preset loaded: Smooth R&B"
     *  - "Sample rate changed to 96000"
     *  @param category  e.g. "parameter", "preset", "audio", "ui"
     *  @param message   Human-readable description
     */
    virtual void addBreadcrumb(const std::string& category,
                               const std::string& message) = 0;

    /** Convenience: called from processBlock to leave an audio-thread
     *  breadcrumb at most once per second (rate-limited). */
    virtual void processBlockBreadcrumb() = 0;

    // ---- User Context ----

    /** Set user context for crash reports (anonymous ID). */
    virtual void setUserId(const std::string& userId) = 0;

    /** Set custom tag (key-value pair) attached to all subsequent reports. */
    virtual void setTag(const std::string& key, const std::string& value) = 0;

    /** Update the host name (call when host info becomes available). */
    virtual void setHostName(const std::string& hostName) = 0;

    /** Update audio context. */
    virtual void setAudioContext(int sampleRate, int bufferSize) = 0;

    // ---- Manual Reporting ----

    /** Manually report a handled exception or assertion failure.
     *  Does NOT terminate the process — just sends a report. */
    virtual void reportException(const std::string& message,
                                 const std::string& category = "manual") = 0;
};


// ============================================================================
// ZNullCrashReporter — No-op implementation for development
// ============================================================================

class ZNullCrashReporter : public ZCrashReporter
{
public:
    ZNullCrashReporter() = default;

    void initialize(const ZCrashReportDetails& /*details*/,
                    const std::string& /*dsn*/ = "") override
    {
        DBG("ZNullCrashReporter: initialized (no-op — no crash reporting active)");
    }

    void shutdown() override {}

    void addBreadcrumb(const std::string& /*category*/,
                       const std::string& /*message*/) override {}

    void processBlockBreadcrumb() override {}

    void setUserId(const std::string& /*userId*/) override {}
    void setTag(const std::string& /*key*/, const std::string& /*value*/) override {}
    void setHostName(const std::string& /*hostName*/) override {}
    void setAudioContext(int /*sampleRate*/, int /*bufferSize*/) override {}

    void reportException(const std::string& /*message*/,
                         const std::string& /*category*/ = "manual") override {}
};


// ============================================================================
// ZCrashReporterFactory — Creates the appropriate reporter for the build config
// ============================================================================

class ZCrashReporterFactory
{
public:
    /** Create a crash reporter.
     *  - In development builds: returns ZNullCrashReporter (no-op).
     *  - In production builds with ZARLINO_CRASH_HANDLER_SENTRY: returns Sentry-backed reporter.
     *  - In production without a backend: returns ZNullCrashReporter with a warning.
     */
    static std::unique_ptr<ZCrashReporter> create()
    {
#if defined(ZARLINO_CRASH_HANDLER_SENTRY)
        return createSentryReporter();   // implemented in ZSentryCrashReporter.cpp
#elif defined(NDEBUG) && !defined(JUCE_UNIT_TESTS)
        // Production build without crash handler — warn once
        static bool warned = false;
        if (!warned)
        {
            DBG("ZCrashReporterFactory: Production build detected but no crash "
                "handler backend configured. Define ZARLINO_CRASH_HANDLER_SENTRY "
                "and link sentry-native to enable crash reporting.");
            warned = true;
        }
        return std::make_unique<ZNullCrashReporter>();
#else
        return std::make_unique<ZNullCrashReporter>();
#endif
    }

private:
    /** Implemented in ZSentryCrashReporter.cpp — requires sentry-native SDK.
     *  This header-declared function will cause a linker error if
     *  ZARLINO_CRASH_HANDLER_SENTRY is defined but the .cpp is not compiled.
     *  See the file header for usage instructions. */
    static std::unique_ptr<ZCrashReporter> createSentryReporter();
};


// ============================================================================
// Convenience: rate-limited breadcrumb helper for processBlock
// ============================================================================

/** Rate-limiter for processBlock breadcrumbs — prevents flooding the crash
 *  report with per-block events while still capturing the last block before
 *  a crash.
 *
 *  Usage inside processBlock:
 *    m_breadcrumbLimiter.tick([&]() {
 *        crashReporter->addBreadcrumb("audio", "processing block");
 *    });
 */
class ZBreadcrumbRateLimiter
{
public:
    using Callback = std::function<void()>;

    /** Call at most once per intervalMs from the audio thread. */
    void tick(Callback&& cb, int intervalMs = 500)
    {
        m_blockCounter++;

        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - m_lastReportTime >= intervalMs)
        {
            m_lastReportTime = now;
            cb();
        }
    }

    /** Force a breadcrumb on the next tick (e.g., when a user action occurs). */
    void force() { m_lastReportTime = 0.0; }

private:
    double m_lastReportTime = 0.0;
    int m_blockCounter = 0;
};
