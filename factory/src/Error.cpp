#include "zfactory/Error.h"

namespace zfactory
{

const char* errorCodeName (ErrorCode code) noexcept
{
    switch (code)
    {
        case ErrorCode::None:                        return "NONE";
        case ErrorCode::JsonParseFailed:             return "JSON_PARSE_FAILED";
        case ErrorCode::RootNotObject:               return "ROOT_NOT_OBJECT";
        case ErrorCode::MissingRequiredField:        return "MISSING_REQUIRED_FIELD";
        case ErrorCode::UnexpectedField:             return "UNEXPECTED_FIELD";
        case ErrorCode::TypeMismatch:                return "TYPE_MISMATCH";
        case ErrorCode::ValueOutOfRange:             return "VALUE_OUT_OF_RANGE";
        case ErrorCode::UnsupportedSchemaVersion:    return "UNSUPPORTED_SCHEMA_VERSION";
        case ErrorCode::UnsupportedValue:            return "UNSUPPORTED_VALUE";
        case ErrorCode::EmptyCollection:             return "EMPTY_COLLECTION";
        case ErrorCode::DuplicateIdentifier:         return "DUPLICATE_IDENTIFIER";
        case ErrorCode::DanglingReference:           return "DANGLING_REFERENCE";
        case ErrorCode::InvalidIdentifier:           return "INVALID_IDENTIFIER";
        case ErrorCode::CollectionTooLarge:          return "COLLECTION_TOO_LARGE";
        case ErrorCode::UnknownModule:               return "UNKNOWN_MODULE";
        case ErrorCode::ModuleVersionIncompatible:   return "MODULE_VERSION_INCOMPATIBLE";
        case ErrorCode::ModuleNotCertified:          return "MODULE_NOT_CERTIFIED";
        case ErrorCode::ModuleChannelUnsupported:    return "MODULE_CHANNEL_UNSUPPORTED";
        case ErrorCode::ModuleSampleRateUnsupported: return "MODULE_SAMPLE_RATE_UNSUPPORTED";
        case ErrorCode::ModuleParameterUnknown:      return "MODULE_PARAMETER_UNKNOWN";
        case ErrorCode::ModuleDependencyMissing:     return "MODULE_DEPENDENCY_MISSING";
        case ErrorCode::ModuleAlreadyRegistered:     return "MODULE_ALREADY_REGISTERED";
        case ErrorCode::GraphEmpty:                  return "GRAPH_EMPTY";
        case ErrorCode::GraphTooLong:                return "GRAPH_TOO_LONG";
        case ErrorCode::GraphChannelMismatch:        return "GRAPH_CHANNEL_MISMATCH";
        case ErrorCode::NotPrepared:                 return "NOT_PREPARED";
        case ErrorCode::InvalidStateBlob:            return "INVALID_STATE_BLOB";
        case ErrorCode::StateSpecMismatch:           return "STATE_SPEC_MISMATCH";
        case ErrorCode::StateSchemaUnsupported:      return "STATE_SCHEMA_UNSUPPORTED";
        case ErrorCode::ChannelCountMismatch:        return "CHANNEL_COUNT_MISMATCH";
        case ErrorCode::BlockSizeExceeded:           return "BLOCK_SIZE_EXCEEDED";
    }

    return "UNKNOWN";
}

std::string FactoryError::describe() const
{
    std::string s = "ZFACTORY-";
    s += std::to_string (static_cast<int> (code));
    s += " ";
    s += path;
    s += ": ";
    s += message;
    return s;
}

} // namespace zfactory
