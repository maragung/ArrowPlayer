// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Application identity — §28 Phase 0 exit gate 7 ("Version string generated
// from git and shown in About"), REQ-BLD-007.
//
// Layer 4 (APPLICATION) per §7.1: pure C++20, no Qt, no adapter.
//
// Why identity lives here and not in the UI: `arrow/version.hpp` is generated
// by CMake from git, and the UI is compiled only when Qt is present. With one
// reader of that generated header in layer 4, the headless binary and the About
// dialog cannot disagree about which build this is. Two readers could.

#pragma once

#include <string>
#include <string_view>

namespace arrow::app {

/// Immutable build identity, read once from the generated version header.
///
/// Trivially copyable and thread-safe to read: every member is a view of a
/// string literal with static storage duration, or a scalar. Safe to copy onto
/// any thread, including one that must not allocate.
struct AppInfo {
    std::string_view name;     ///< arrow::version::kName
    std::string_view version;  ///< arrow::version::kString  ("0.1.0")
    std::string_view git_sha;  ///< arrow::version::kGitSha  (short hash)
    bool git_dirty{false};     ///< the tree had uncommitted changes
    int major{0};
    int minor{0};
    int patch{0};

    /// The identity of the running binary.
    [[nodiscard]] static AppInfo current() noexcept;

    /// Untranslated single-line rendering, for logs and bug reports.
    ///
    /// Deliberately *not* the text the About dialog shows. REQ-UIX-070 requires
    /// every user-visible string to be externalised, and REQ-UIX-078 forbids
    /// assembling one from fragments — a sentence composed here would reach the
    /// UI as an opaque blob no translator can reorder. So the UI receives the
    /// fields and composes its own translated text; this serves logs, which
    /// §22.2 specifies as machine-parseable rather than localised.
    [[nodiscard]] std::string to_log_string() const;

    /// True when the build came from a tree that was not clean. Kept as its own
    /// predicate because a dirty build is the single most useful fact in a bug
    /// report and the easiest one to drop by accident when formatting.
    [[nodiscard]] bool is_reproducible_build() const noexcept { return !git_dirty; }
};

}  // namespace arrow::app
