#pragma once

#include <optional>
#include <string>

namespace zfactory
{

//==============================================================================
// Deterministic error reporting.
//
// Every failure path in the factory kernel produces a FactoryError carrying a
// stable numeric code, a JSON-pointer style location, and a message built only
// from the input values. Feeding the same input to the kernel twice therefore
// produces the same error code, the same path and the same message - this is
// asserted by the factory test suite.
//==============================================================================

enum class ErrorCode : int
{
    None = 0,

    // ---- PluginSpec document shape (1xxx) ----
    JsonParseFailed              = 1001,
    RootNotObject                = 1002,
    MissingRequiredField         = 1003,
    UnexpectedField              = 1004,
    TypeMismatch                 = 1005,
    ValueOutOfRange              = 1006,
    UnsupportedSchemaVersion     = 1007,
    UnsupportedValue             = 1008,
    EmptyCollection              = 1009,
    DuplicateIdentifier          = 1010,
    DanglingReference            = 1011,
    InvalidIdentifier            = 1012,
    CollectionTooLarge           = 1013,

    // ---- Module resolution (2xxx) ----
    UnknownModule                = 2001,
    ModuleVersionIncompatible    = 2002,
    ModuleNotCertified           = 2003,
    ModuleChannelUnsupported     = 2004,
    ModuleSampleRateUnsupported  = 2005,
    ModuleParameterUnknown       = 2006,
    ModuleDependencyMissing      = 2007,
    ModuleAlreadyRegistered      = 2008,

    // ---- Graph construction (3xxx) ----
    GraphEmpty                   = 3001,
    GraphTooLong                 = 3002,
    GraphChannelMismatch         = 3003,

    // ---- Runtime / state (4xxx) ----
    NotPrepared                  = 4001,
    InvalidStateBlob             = 4002,
    StateSpecMismatch            = 4003,
    StateSchemaUnsupported       = 4004,
    ChannelCountMismatch         = 4005,
    BlockSizeExceeded            = 4006
};

/** Stable machine-readable name for an error code. Never localised. */
const char* errorCodeName (ErrorCode code) noexcept;

//==============================================================================

struct FactoryError
{
    ErrorCode code = ErrorCode::None;
    std::string path;     // "$", "$/modules[2]/moduleId", ...
    std::string message;  // deterministic detail derived from the input only

    bool ok() const noexcept { return code == ErrorCode::None; }
    bool failed() const noexcept { return code != ErrorCode::None; }

    /** "ZFACTORY-1003 $/modules[0]/moduleId: required field is missing" */
    std::string describe() const;

    bool operator== (const FactoryError& other) const noexcept
    {
        return code == other.code && path == other.path && message == other.message;
    }
    bool operator!= (const FactoryError& other) const noexcept { return ! (*this == other); }
};

inline FactoryError makeError (ErrorCode code, std::string path, std::string message)
{
    FactoryError e;
    e.code = code;
    e.path = std::move (path);
    e.message = std::move (message);
    return e;
}

//==============================================================================
/** Value-or-error carrier. On success `value()` is populated; on failure
    `error()` holds the deterministic failure description. */
template <typename T>
class Result
{
public:
    static Result success (T value) { return Result (std::move (value)); }
    static Result failure (FactoryError error) { return Result (std::move (error)); }

    bool ok() const noexcept { return m_error.ok(); }
    explicit operator bool() const noexcept { return ok(); }

    const T& value() const noexcept { return *m_value; }
    T& value() noexcept { return *m_value; }
    T&& takeValue() noexcept { return std::move (*m_value); }

    const FactoryError& error() const noexcept { return m_error; }

private:
    explicit Result (T value) : m_value (std::move (value)) {}
    explicit Result (FactoryError error) : m_error (std::move (error)) {}

    std::optional<T> m_value;
    FactoryError m_error;
};

} // namespace zfactory
