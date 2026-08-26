// SPDX-License-Identifier: MPL-2.0
#include "core/error.hpp"

#include <string>

namespace eclipse {

std::string_view to_string(Severity sev) noexcept {
    switch (sev) {
        case Severity::Trace:    return "trace";
        case Severity::Debug:    return "debug";
        case Severity::Info:     return "info";
        case Severity::Notice:   return "notice";
        case Severity::Warning:  return "warn";
        case Severity::Error:    return "error";
        case Severity::Critical: return "critical";
    }
    return "error";
}

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Ok:                       return "OK";
        case ErrorCode::Unknown:                  return "UNKNOWN";
        case ErrorCode::NotImplemented:           return "NOT_IMPLEMENTED";
        case ErrorCode::InvalidArgument:          return "INVALID_ARGUMENT";
        case ErrorCode::OutOfRange:               return "OUT_OF_RANGE";
        case ErrorCode::Cancelled:                return "CANCELLED";
        case ErrorCode::Timeout:                  return "TIMEOUT";
        case ErrorCode::ResourceExhausted:        return "RESOURCE_EXHAUSTED";
        case ErrorCode::InvalidState:             return "INVALID_STATE";

        case ErrorCode::FileNotFound:             return "FILE_NOT_FOUND";
        case ErrorCode::PermissionDenied:         return "PERMISSION_DENIED";
        case ErrorCode::PathTooLong:              return "PATH_TOO_LONG";
        case ErrorCode::PathTraversal:            return "PATH_TRAVERSAL";
        case ErrorCode::DiskFull:                 return "DISK_FULL";
        case ErrorCode::IoError:                  return "IO_ERROR";
        case ErrorCode::NotADirectory:            return "NOT_A_DIRECTORY";

        case ErrorCode::UnsupportedFormat:        return "UNSUPPORTED_FORMAT";
        case ErrorCode::CorruptStream:            return "CORRUPT_STREAM";
        case ErrorCode::DecoderInitFailed:        return "DECODER_INIT_FAILED";
        case ErrorCode::SeekFailed:               return "SEEK_FAILED";
        case ErrorCode::NoAudioStream:            return "NO_AUDIO_STREAM";

        case ErrorCode::DeviceNotFound:           return "DEVICE_NOT_FOUND";
        case ErrorCode::DeviceInUse:              return "DEVICE_IN_USE";
        case ErrorCode::DeviceFormatUnsupported:  return "DEVICE_FORMAT_UNSUPPORTED";
        case ErrorCode::DeviceLost:               return "DEVICE_LOST";
        case ErrorCode::ExclusiveModeUnavailable: return "EXCLUSIVE_MODE_UNAVAILABLE";
        case ErrorCode::BitPerfectUnavailable:    return "BIT_PERFECT_UNAVAILABLE";
        case ErrorCode::BufferUnderrun:           return "BUFFER_UNDERRUN";

        case ErrorCode::ParseError:               return "PARSE_ERROR";
        case ErrorCode::UnexpectedToken:          return "UNEXPECTED_TOKEN";
        case ErrorCode::UnexpectedEnd:            return "UNEXPECTED_END";
        case ErrorCode::MalformedTimestamp:       return "MALFORMED_TIMESTAMP";
        case ErrorCode::MalformedHeader:          return "MALFORMED_HEADER";
        case ErrorCode::ChecksumMismatch:         return "CHECKSUM_MISMATCH";
        case ErrorCode::InputTooLarge:            return "INPUT_TOO_LARGE";
        case ErrorCode::NestingTooDeep:           return "NESTING_TOO_DEEP";
        case ErrorCode::OutputCapExceeded:        return "OUTPUT_CAP_EXCEEDED";

        case ErrorCode::SchemaViolation:          return "SCHEMA_VIOLATION";
        case ErrorCode::SchemaVersionTooNew:      return "SCHEMA_VERSION_TOO_NEW";
        case ErrorCode::AppVersionTooOld:         return "APP_VERSION_TOO_OLD";
        case ErrorCode::UnknownComponent:         return "UNKNOWN_COMPONENT";
        case ErrorCode::UnknownBinding:           return "UNKNOWN_BINDING";
        case ErrorCode::UnknownAction:            return "UNKNOWN_ACTION";
        case ErrorCode::ContrastBelowFloor:       return "CONTRAST_BELOW_FLOOR";
        case ErrorCode::ResourceBudgetExceeded:   return "RESOURCE_BUDGET_EXCEEDED";
        case ErrorCode::ZipSlipDetected:          return "ZIP_SLIP_DETECTED";
        case ErrorCode::ZipBombDetected:          return "ZIP_BOMB_DETECTED";
        case ErrorCode::UnsafeSvg:                return "UNSAFE_SVG";
        case ErrorCode::MissingRequiredFile:      return "MISSING_REQUIRED_FILE";

        case ErrorCode::DatabaseCorrupt:          return "DATABASE_CORRUPT";
        case ErrorCode::MigrationFailed:          return "MIGRATION_FAILED";
        case ErrorCode::ConstraintViolation:      return "CONSTRAINT_VIOLATION";
        case ErrorCode::QueryFailed:              return "QUERY_FAILED";

        case ErrorCode::NetworkDisabled:          return "NETWORK_DISABLED";
        case ErrorCode::NetworkUnreachable:       return "NETWORK_UNREACHABLE";
        case ErrorCode::TlsError:                 return "TLS_ERROR";
        case ErrorCode::HttpError:                return "HTTP_ERROR";
        case ErrorCode::RateLimited:              return "RATE_LIMITED";
    }
    return "UNKNOWN";
}

std::string Error::to_log_string() const {
    std::string out;
    out.reserve(user_message_.size() + technical_detail_.size() + 64);
    out += '[';
    out += to_string(severity_);
    out += "] ";
    out += to_string(code_);
    out += ": ";
    out += user_message_;
    if (!technical_detail_.empty()) {
        out += " | ";
        out += technical_detail_;
    }
    if (line_ > 0) {
        out += " (line ";
        out += std::to_string(line_);
        out += ", col ";
        out += std::to_string(column_);
        out += ')';
    } else if (offset_ > 0) {
        out += " (offset ";
        out += std::to_string(offset_);
        out += ')';
    }
    return out;
}

}  // namespace eclipse
