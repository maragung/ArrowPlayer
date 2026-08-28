// SPDX-License-Identifier: MPL-2.0
// Application-layer tests — §1.3 rule 2: happy path, every documented failure
// mode, every boundary.
//
// The failure modes here are the ones Lifecycle's header documents, one test
// each. They are cheap now and impossible to retrofit later: by Phase 6 the
// startup list holds the audio engine, the library scanner and the plugin host,
// and "a failed start left the two before it running" is not a bug anyone finds
// by reading.

#include <string>
#include <vector>

#include <arrow/version.hpp>

#include "app/app_info.hpp"
#include "app/application.hpp"
#include "app/lifecycle.hpp"

#include <gtest/gtest.h>

using arrow::ErrorCode;
using arrow::app::AppInfo;
using arrow::app::Application;
using arrow::app::Lifecycle;
using arrow::app::LifecycleState;

// --------------------------------------------------------------------- AppInfo

TEST(AppInfo, CurrentMatchesTheGeneratedHeader) {
    // Exit gate 7 requires the displayed version to be the value CMake generated
    // from git (REQ-BLD-007), so this asserts identity with that header rather
    // than against a literal — a literal here would pass while the About dialog
    // showed something else.
    const AppInfo info = AppInfo::current();
    EXPECT_EQ(info.name, arrow::version::kName);
    EXPECT_EQ(info.version, arrow::version::kString);
    EXPECT_EQ(info.git_sha, arrow::version::kGitSha);
    EXPECT_EQ(info.git_dirty, arrow::version::kGitDirty != 0);
    EXPECT_EQ(info.major, arrow::version::kMajor);
    EXPECT_EQ(info.minor, arrow::version::kMinor);
    EXPECT_EQ(info.patch, arrow::version::kPatch);
    EXPECT_FALSE(info.version.empty());
    EXPECT_FALSE(info.name.empty());
}

TEST(AppInfo, LogStringCarriesEverythingABugReportNeeds) {
    const AppInfo info{.name = "Arrow Player",
                       .version = "1.2.3",
                       .git_sha = "abcdef123456",
                       .git_dirty = false,
                       .major = 1,
                       .minor = 2,
                       .patch = 3};
    const std::string s = info.to_log_string();
    EXPECT_NE(s.find("Arrow Player"), std::string::npos);
    EXPECT_NE(s.find("1.2.3"), std::string::npos);
    EXPECT_NE(s.find("abcdef123456"), std::string::npos);
    EXPECT_EQ(s.find("modified"), std::string::npos);
    EXPECT_TRUE(info.is_reproducible_build());
}

TEST(AppInfo, DirtyTreeIsStatedInWordsNotDropped) {
    AppInfo info{.name = "Arrow Player", .version = "1.2.3", .git_sha = "abcdef123456"};
    info.git_dirty = true;
    EXPECT_NE(info.to_log_string().find("working tree modified"), std::string::npos);
    EXPECT_FALSE(info.is_reproducible_build());
}

// ------------------------------------------------------------------- Lifecycle
//  Boundary: no steps at all. This is Phase 0's actual configuration.

TEST(Lifecycle, EmptyLifecycleStartsAndStops) {
    Lifecycle lc;
    EXPECT_EQ(lc.state(), LifecycleState::Created);
    EXPECT_EQ(lc.step_count(), 0U);
    ASSERT_TRUE(lc.start());
    EXPECT_EQ(lc.state(), LifecycleState::Running);
    EXPECT_TRUE(lc.started().empty());
    ASSERT_TRUE(lc.shutdown());
    EXPECT_EQ(lc.state(), LifecycleState::Stopped);
}

TEST(Lifecycle, StepsStartInOrderAndStopInReverse) {
    std::vector<std::string> trace;
    Lifecycle lc;
    for (const char* name : {"first", "second", "third"}) {
        const std::string step{name};
        ASSERT_TRUE(lc.add_step(
            step,
            [&trace, step] {
                trace.push_back("start:" + step);
                return arrow::ok();
            },
            [&trace, step] { trace.push_back("stop:" + step); }));
    }
    ASSERT_TRUE(lc.start());
    EXPECT_EQ(lc.started(), (std::vector<std::string>{"first", "second", "third"}));
    ASSERT_TRUE(lc.shutdown());
    EXPECT_EQ(trace,
              (std::vector<std::string>{"start:first",
                                        "start:second",
                                        "start:third",
                                        "stop:third",
                                        "stop:second",
                                        "stop:first"}));
    EXPECT_TRUE(lc.started().empty());
}

TEST(Lifecycle, AStepMayOmitItsTeardown) {
    Lifecycle lc;
    ASSERT_TRUE(lc.add_step("no-teardown", [] { return arrow::ok(); }));
    ASSERT_TRUE(lc.start());
    ASSERT_TRUE(lc.shutdown());
}

//  Documented failure mode: a step that fails to start.

TEST(Lifecycle, FailedStartUnwindsOnlyWhatStarted) {
    std::vector<std::string> trace;
    Lifecycle lc;
    ASSERT_TRUE(lc.add_step(
        "ok-one",
        [&trace] {
            trace.push_back("start:ok-one");
            return arrow::ok();
        },
        [&trace] { trace.push_back("stop:ok-one"); }));
    ASSERT_TRUE(lc.add_step(
        "ok-two",
        [&trace] {
            trace.push_back("start:ok-two");
            return arrow::ok();
        },
        [&trace] { trace.push_back("stop:ok-two"); }));
    ASSERT_TRUE(lc.add_step(
        "broken",
        [&trace] {
            trace.push_back("start:broken");
            return arrow::Status{arrow::err(
                ErrorCode::DeviceNotFound, "No audio device was found.", "no sink")};
        },
        [&trace] { trace.push_back("stop:broken"); }));

    const arrow::Status result = lc.start();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::DeviceNotFound);
    // The failing step is named, so the log says which subsystem died.
    EXPECT_NE(result.error().technical_detail().find("broken"), std::string::npos);
    EXPECT_EQ(lc.state(), LifecycleState::Failed);
    EXPECT_TRUE(lc.started().empty());
    // 'broken' never reported a successful start, so its teardown must not run.
    EXPECT_EQ(
        trace,
        (std::vector<std::string>{
            "start:ok-one", "start:ok-two", "start:broken", "stop:ok-two", "stop:ok-one"}));
}

TEST(Lifecycle, FailedIsTerminal) {
    Lifecycle lc;
    ASSERT_TRUE(lc.add_step("broken", [] {
        return arrow::Status{arrow::err(ErrorCode::IoError, "Failed.", "d")};
    }));
    ASSERT_FALSE(lc.start());
    EXPECT_EQ(lc.start().error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.shutdown().error().code(), ErrorCode::InvalidState);
}

//  Documented failure modes: illegal transitions and illegal registrations.

TEST(Lifecycle, RejectsAStepWithNoName) {
    Lifecycle lc;
    const arrow::Status s = lc.add_step("", [] { return arrow::ok(); });
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(lc.step_count(), 0U);
}

TEST(Lifecycle, RejectsAStepWithNothingToRun) {
    Lifecycle lc;
    const arrow::Status s = lc.add_step("empty", Lifecycle::StartFn{});
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(lc.step_count(), 0U);
}

TEST(Lifecycle, RejectsAStepAddedAfterStart) {
    Lifecycle lc;
    ASSERT_TRUE(lc.start());
    const arrow::Status s = lc.add_step("late", [] { return arrow::ok(); });
    ASSERT_FALSE(s);
    EXPECT_EQ(s.error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.step_count(), 0U);
}

TEST(Lifecycle, RejectsASecondStart) {
    Lifecycle lc;
    ASSERT_TRUE(lc.start());
    EXPECT_EQ(lc.start().error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.state(), LifecycleState::Running);
}

TEST(Lifecycle, RejectsShutdownBeforeStart) {
    Lifecycle lc;
    EXPECT_EQ(lc.shutdown().error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.state(), LifecycleState::Created);
}

TEST(Lifecycle, RejectsASecondShutdownAndARestart) {
    Lifecycle lc;
    ASSERT_TRUE(lc.start());
    ASSERT_TRUE(lc.shutdown());
    EXPECT_EQ(lc.shutdown().error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.start().error().code(), ErrorCode::InvalidState);
    EXPECT_EQ(lc.state(), LifecycleState::Stopped);
}

TEST(Lifecycle, EveryStateHasAName) {
    // A log line reading "state: unknown" is a log line that cannot be acted on.
    for (const LifecycleState state : {LifecycleState::Created,
                                       LifecycleState::Starting,
                                       LifecycleState::Running,
                                       LifecycleState::ShuttingDown,
                                       LifecycleState::Stopped,
                                       LifecycleState::Failed}) {
        EXPECT_NE(arrow::app::to_string(state), "unknown");
    }
}

// ----------------------------------------------------------------- Application

TEST(Application, CarriesTheIdentityItWasGiven) {
    Application app{AppInfo::current()};
    EXPECT_EQ(app.info().version, arrow::version::kString);
    EXPECT_EQ(app.lifecycle().state(), LifecycleState::Created);
}

TEST(Application, ExitCodesDistinguishBrokenProgramFromMissingEnvironment) {
    using arrow::err;
    EXPECT_EQ(Application::exit_code_for(err(ErrorCode::Ok, "")), Application::kExitOk);
    EXPECT_EQ(Application::exit_code_for(err(ErrorCode::DeviceNotFound, "")),
              Application::kExitUnavailable);
    EXPECT_EQ(Application::exit_code_for(err(ErrorCode::PermissionDenied, "")),
              Application::kExitUnavailable);
    EXPECT_EQ(Application::exit_code_for(err(ErrorCode::ParseError, "")),
              Application::kExitStartupFailed);
    // Distinct values, or a wrapper script cannot branch on them.
    EXPECT_NE(Application::kExitUnavailable, Application::kExitStartupFailed);
    EXPECT_NE(Application::kExitOk, Application::kExitStartupFailed);
}
