// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// The application object — layer 4 (APPLICATION) per §7.1, named by §5's layout
// for `desktop/src/app/`.
//
// It owns the two things that outlive every subsystem: the build's identity
// (§28 Phase 0 exit gate 7) and the startup ordering. It deliberately does NOT
// own the UI: §7.2 rule 1 says layer 4 must not depend on layer 5, so nothing
// here may name the Qt shell. main.cpp is the composition root and is the only
// translation unit allowed to see both.
//
// §5 also lists a DI container for this directory. It is not here yet, and the
// reason is recorded as OQ-054 in docs/OPEN-QUESTIONS.md — Phase 0 has no ports
// to register, so its interface would be designed against nothing.

#pragma once

#include "app/app_info.hpp"
#include "app/lifecycle.hpp"
#include "core/error.hpp"

namespace eclipse::app {

/// Process-wide application state. One instance, owned by main().
///
/// Not thread-safe; see Lifecycle. Construct and drive it from the main thread.
class Application {
public:
    // Exit codes. The values follow the BSD sysexits convention rather than
    // being invented here, because a shell script or a systemd unit that wraps
    // the player can act on them, and 1-for-everything cannot be acted on.
    static constexpr int kExitOk            = 0;
    static constexpr int kExitStartupFailed = 70;  ///< EX_SOFTWARE
    static constexpr int kExitUnavailable   = 69;  ///< EX_UNAVAILABLE

    explicit Application(AppInfo info) noexcept : info_{info} {}

    [[nodiscard]] const AppInfo& info() const noexcept { return info_; }
    [[nodiscard]] Lifecycle&     lifecycle() noexcept { return lifecycle_; }

    /// Process exit code for an error that reached main().
    ///
    /// The mapping is policy, and it belongs to layer 4 rather than to main.cpp
    /// so the headless path and the UI path cannot report the same failure with
    /// two different codes.
    [[nodiscard]] static int exit_code_for(const Error& error) noexcept;

private:
    AppInfo   info_;
    Lifecycle lifecycle_;
};

}  // namespace eclipse::app
