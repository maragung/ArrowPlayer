// SPDX-License-Identifier: MPL-2.0
// Tests for core/error.hpp — spec §22.1 (REQ-GEN-060 .. REQ-GEN-063).

#include <string>

#include "core/error.hpp"

#include <gtest/gtest.h>

using namespace eclipse;

TEST(Error, CarriesCodeMessageAndSeverity) {
    const Error e{ErrorCode::FileNotFound,
                  "That file could not be found.",
                  "/music/a.flac",
                  Severity::Warning,
                  RecoveryAction::Rescan};
    EXPECT_EQ(e.code(), ErrorCode::FileNotFound);
    EXPECT_EQ(e.user_message(), "That file could not be found.");
    EXPECT_EQ(e.technical_detail(), "/music/a.flac");
    EXPECT_EQ(e.severity(), Severity::Warning);
    EXPECT_EQ(e.recovery(), RecoveryAction::Rescan);
}

TEST(Error, UserMessageNeverContainsANumericCode) {
    // REQ-GEN-063 forbids code-only messages. Guard the shape of the API:
    // the user message and the stable code are separate fields by construction.
    const Error e{ErrorCode::CorruptStream, "This track could not be played."};
    EXPECT_EQ(e.user_message().find("0x"), std::string::npos);
    EXPECT_EQ(e.user_message().find("code"), std::string::npos);
    EXPECT_FALSE(e.user_message().empty());
}

TEST(Error, EveryCodeHasAStableStableName) {
    // A log or bug report must never contain "UNKNOWN" for a code we defined.
    const ErrorCode codes[] = {
        ErrorCode::Ok,
        ErrorCode::InvalidArgument,
        ErrorCode::Cancelled,
        ErrorCode::InvalidState,
        ErrorCode::FileNotFound,
        ErrorCode::PathTraversal,
        ErrorCode::UnsupportedFormat,
        ErrorCode::CorruptStream,
        ErrorCode::DeviceInUse,
        ErrorCode::ExclusiveModeUnavailable,
        ErrorCode::BitPerfectUnavailable,
        ErrorCode::BufferUnderrun,
        ErrorCode::ParseError,
        ErrorCode::MalformedTimestamp,
        ErrorCode::OutputCapExceeded,
        ErrorCode::NestingTooDeep,
        ErrorCode::SchemaViolation,
        ErrorCode::ContrastBelowFloor,
        ErrorCode::ZipSlipDetected,
        ErrorCode::ZipBombDetected,
        ErrorCode::UnsafeSvg,
        ErrorCode::ResourceBudgetExceeded,
        ErrorCode::DatabaseCorrupt,
        ErrorCode::MigrationFailed,
        ErrorCode::NetworkDisabled,
        ErrorCode::RateLimited,
    };
    for (const ErrorCode c : codes) {
        const auto name = to_string(c);
        EXPECT_FALSE(name.empty());
        if (c != ErrorCode::Unknown) {
            EXPECT_NE(name, "UNKNOWN") << "missing to_string for code " << static_cast<int>(c);
        }
    }
}

TEST(Error, SeverityNames) {
    EXPECT_EQ(to_string(Severity::Trace), "trace");
    EXPECT_EQ(to_string(Severity::Warning), "warn");
    EXPECT_EQ(to_string(Severity::Critical), "critical");
}

TEST(Error, PositionInformationForParsers) {
    Error e{ErrorCode::UnexpectedToken, "Unexpected ']'."};
    e.at(42, 3, 17);
    EXPECT_EQ(e.offset(), 42u);
    EXPECT_EQ(e.line(), 3u);
    EXPECT_EQ(e.column(), 17u);
    const auto log = e.to_log_string();
    EXPECT_NE(log.find("line 3"), std::string::npos);
    EXPECT_NE(log.find("col 17"), std::string::npos);
}

TEST(Error, LogStringIncludesCodeAndSeverity) {
    const Error e{ErrorCode::DeviceLost,
                  "The audio device was disconnected.",
                  "wasapi: AUDCLNT_E_DEVICE_INVALIDATED"};
    const auto log = e.to_log_string();
    EXPECT_NE(log.find("DEVICE_LOST"), std::string::npos);
    EXPECT_NE(log.find("[error]"), std::string::npos);
    EXPECT_NE(log.find("AUDCLNT_E_DEVICE_INVALIDATED"), std::string::npos);
}

TEST(Error, BuilderMethodsChain) {
    Error e = err(ErrorCode::BufferUnderrun, "Audio playback stuttered.");
    e.with_severity(Severity::Notice)
        .with_recovery(RecoveryAction::IncreaseBuffer)
        .with_detail("underruns=4 window=10s");
    EXPECT_EQ(e.severity(), Severity::Notice);
    EXPECT_EQ(e.recovery(), RecoveryAction::IncreaseBuffer);
    EXPECT_EQ(e.technical_detail(), "underruns=4 window=10s");
}

// ===========================================================================
//  Result<T>
// ===========================================================================

TEST(Result, HoldsAValue) {
    Result<int> r{7};
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(static_cast<bool>(r));
    EXPECT_EQ(r.value(), 7);
    EXPECT_EQ(*r, 7);
    EXPECT_EQ(r.value_or(99), 7);
}

TEST(Result, HoldsAnError) {
    Result<int> r{err(ErrorCode::Timeout, "It took too long.")};
    ASSERT_FALSE(r.has_value());
    ASSERT_FALSE(static_cast<bool>(r));
    EXPECT_EQ(r.error().code(), ErrorCode::Timeout);
    EXPECT_EQ(r.value_or(99), 99);
}

TEST(Result, WorksWithMoveOnlyTypes) {
    Result<std::unique_ptr<int>> r{std::make_unique<int>(5)};
    ASSERT_TRUE(r.has_value());
    auto ptr = std::move(r).value();
    EXPECT_EQ(*ptr, 5);
}

TEST(Result, StatusAndOk) {
    Status s = ok();
    EXPECT_TRUE(s.has_value());

    Status bad = err(ErrorCode::PermissionDenied, "No permission.");
    EXPECT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().code(), ErrorCode::PermissionDenied);
}

TEST(Result, ArrowOperatorReachesTheValue) {
    Result<std::string> r{std::string{"hello"}};
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 5u);
}

TEST(Result, PropagatesThroughCallChain) {
    // The intended usage pattern: errors flow back as values, never exceptions.
    const auto step = [](bool fail) -> Result<int> {
        if (fail) return err(ErrorCode::CorruptStream, "Bad data.");
        return 1;
    };
    const auto pipeline = [&](bool fail) -> Result<int> {
        auto a = step(fail);
        if (!a) return std::move(a).error();
        return a.value() + 10;
    };
    EXPECT_EQ(pipeline(false).value(), 11);
    EXPECT_EQ(pipeline(true).error().code(), ErrorCode::CorruptStream);
}
