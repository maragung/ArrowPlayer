// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// Error taxonomy — spec §22.1 (REQ-GEN-060 .. REQ-GEN-063).
//
// Errors are values, never exceptions across a port boundary, and never on the
// real-time audio thread (REQ-AUD-015). Every error carries a stable code, a
// user-facing message, a technical detail, a severity, and an optional recovery
// action the UI may offer as a button.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace eclipse {

/// Severity ladder. §22.1 REQ-GEN-062 binds each level to a required UI
/// behaviour; the UI layer must not invent its own mapping.
enum class Severity {
    Trace,
    Debug,
    Info,
    Notice,   ///< non-blocking, auto-dismissing inline notice
    Warning,  ///< persistent dismissible banner naming the degradation
    Error,    ///< inline error with a retry/fix action
    Critical  ///< modal, with a clear next step
};

/// A recovery action the UI may surface. Enum-only: the error layer never
/// hands the UI a callback, so errors stay copyable and loggable.
enum class RecoveryAction {
    None,
    Retry,
    SkipTrack,
    ChooseAnotherDevice,
    ReopenDevice,
    IncreaseBuffer,
    Rescan,
    GrantPermission,
    OpenSettings,
    RestoreDefaults,
    ViewLog
};

/// Stable, namespaced error codes. The numeric value is never displayed to the
/// user (REQ-GEN-063 forbids code-only messages) but is stable for logs, tests
/// and bug reports.
enum class ErrorCode {
    Ok = 0,

    // ---- generic ----------------------------------------------------------
    Unknown = 1,
    NotImplemented,
    InvalidArgument,
    OutOfRange,
    Cancelled,
    Timeout,
    ResourceExhausted,
    InvalidState,  ///< the operation is legal, the object's state is not

    // ---- filesystem -------------------------------------------------------
    FileNotFound = 100,
    PermissionDenied,
    PathTooLong,
    PathTraversal,
    DiskFull,
    IoError,
    NotADirectory,

    // ---- decode / audio (§8.3 REQ-AUD-027) --------------------------------
    UnsupportedFormat = 200,
    CorruptStream,
    DecoderInitFailed,
    SeekFailed,
    NoAudioStream,

    // ---- audio device (§8.10) ---------------------------------------------
    DeviceNotFound = 300,
    DeviceInUse,  ///< exclusive mode held by another application
    DeviceFormatUnsupported,
    DeviceLost,
    ExclusiveModeUnavailable,
    BitPerfectUnavailable,
    BufferUnderrun,

    // ---- parsing (all fuzz targets, §21.6) --------------------------------
    ParseError = 400,
    UnexpectedToken,
    UnexpectedEnd,
    MalformedTimestamp,
    MalformedHeader,
    ChecksumMismatch,
    InputTooLarge,
    NestingTooDeep,
    OutputCapExceeded,  ///< REQ-EFS-009

    // ---- theme / skin (§11.5) ---------------------------------------------
    SchemaViolation = 500,
    SchemaVersionTooNew,
    AppVersionTooOld,
    UnknownComponent,
    UnknownBinding,
    UnknownAction,
    ContrastBelowFloor,      ///< REQ-THM-041
    ResourceBudgetExceeded,  ///< REQ-THM-033
    ZipSlipDetected,         ///< REQ-THM-018
    ZipBombDetected,         ///< REQ-THM-017
    UnsafeSvg,               ///< REQ-THM-042
    MissingRequiredFile,

    // ---- database (§9.4) --------------------------------------------------
    DatabaseCorrupt = 600,
    MigrationFailed,
    ConstraintViolation,
    QueryFailed,

    // ---- network (§17) ----------------------------------------------------
    NetworkDisabled = 700,  ///< REQ-NET-001 global switch is off
    NetworkUnreachable,
    TlsError,
    HttpError,
    RateLimited,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;
[[nodiscard]] std::string_view to_string(Severity sev) noexcept;

/// An error value. Cheap to move, safe to copy, always loggable.
// NOLINTNEXTLINE(clang-analyzer-core.uninitialized.Assign) — false positive: every member
// carries a default initializer (code_ defaults to ErrorCode::Unknown), and the analyzer
// mis-models `Error() = default` with NSDMI, reporting an uninitialized assignment that
// cannot happen. Seen with the clang-generated compile database on the first green run.
class Error {
  public:
    Error() = default;

    Error(ErrorCode code, std::string user_message)
          : code_{code}, user_message_{std::move(user_message)} {}

    Error(ErrorCode code, std::string user_message, std::string technical_detail)
          : code_{code},
            user_message_{std::move(user_message)},
            technical_detail_{std::move(technical_detail)} {}

    Error(ErrorCode code,
          std::string user_message,
          std::string technical_detail,
          Severity severity,
          RecoveryAction recovery = RecoveryAction::None)
          : code_{code},
            user_message_{std::move(user_message)},
            technical_detail_{std::move(technical_detail)},
            severity_{severity},
            recovery_{recovery} {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

    [[nodiscard]] Severity severity() const noexcept { return severity_; }

    [[nodiscard]] RecoveryAction recovery() const noexcept { return recovery_; }

    /// Translated, actionable, jargon-free. Never contains a numeric code.
    [[nodiscard]] const std::string& user_message() const noexcept { return user_message_; }

    /// For logs only. May contain paths, so it is subject to the redaction
    /// rules in REQ-SET-013 before it is written at info level or above.
    [[nodiscard]] const std::string& technical_detail() const noexcept {
        return technical_detail_;
    }

    Error& with_severity(Severity s) noexcept {
        severity_ = s;
        return *this;
    }

    Error& with_recovery(RecoveryAction a) noexcept {
        recovery_ = a;
        return *this;
    }

    Error& with_detail(std::string d) {
        technical_detail_ = std::move(d);
        return *this;
    }

    /// Position information for parser errors, so an editor can point at the
    /// offending character rather than saying "invalid input".
    Error& at(std::size_t offset, std::size_t line = 0, std::size_t column = 0) noexcept {
        offset_ = offset;
        line_ = line;
        column_ = column;
        return *this;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

    [[nodiscard]] std::size_t column() const noexcept { return column_; }

    /// Single-line rendering for logs and test failure output.
    [[nodiscard]] std::string to_log_string() const;

  private:
    ErrorCode code_{ErrorCode::Unknown};
    std::string user_message_;
    std::string technical_detail_;
    Severity severity_{Severity::Error};
    RecoveryAction recovery_{RecoveryAction::None};
    std::size_t offset_{0};
    std::size_t line_{0};
    std::size_t column_{0};
};

// ---------------------------------------------------------------------------
//  Result<T> — the return type for every fallible operation.
// ---------------------------------------------------------------------------

/// Tag type so `Result<void>` works without a special case.
struct Unit {
    friend bool operator==(Unit, Unit) noexcept { return true; }
};

template<typename T>
class [[nodiscard]] Result {
  public:
    using value_type = T;

    Result(T value) : storage_{std::move(value)} {}  // NOLINT(*-explicit-*)

    Result(Error error) : storage_{std::move(error)} {}  // NOLINT(*-explicit-*)

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & { return std::get<0>(storage_); }

    [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }

    [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

    [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }

    [[nodiscard]] Error&& error() && { return std::get<1>(std::move(storage_)); }

    [[nodiscard]] T value_or(T fallback) const& {
        return has_value() ? std::get<0>(storage_) : std::move(fallback);
    }

    [[nodiscard]] T* operator->() { return &std::get<0>(storage_); }

    [[nodiscard]] const T* operator->() const { return &std::get<0>(storage_); }

    [[nodiscard]] T& operator*() & { return std::get<0>(storage_); }

    [[nodiscard]] const T& operator*() const& { return std::get<0>(storage_); }

  private:
    std::variant<T, Error> storage_;
};

using Status = Result<Unit>;

[[nodiscard]] inline Status ok() noexcept {
    return Status{Unit{}};
}

/// Convenience constructors, so call sites stay readable.
[[nodiscard]] inline Error err(ErrorCode code, std::string user_message) {
    return Error{code, std::move(user_message)};
}

[[nodiscard]] inline Error err(ErrorCode code, std::string user_message, std::string detail) {
    return Error{code, std::move(user_message), std::move(detail)};
}

}  // namespace eclipse
