// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Shell infrastructure — spec §7.1 layer 5 (PRESENTATION).  Declares
// ShellInfo, run_shell(), and the internal ::shell namespace.  Only shell.cpp
// and main.cpp may include this header; nothing in src/app/ or below may name
// it.

#pragma once

#include <string_view>

namespace arrow::ui {

/// Build identity passed into the shell by the composition root (main.cpp).
/// The strings are std::string_view handles into AppInfo's owned storage; they
/// are safe to copy because each view points into a std::string that lives for
/// the duration of the process.
struct ShellInfo {
    std::string_view version;   ///< Human-readable version, e.g. "1.2.3"
    std::string_view git_sha;   ///< Short git SHA-1 of the build commit
    bool git_dirty = false;    ///< true when the working tree had uncommitted changes
};

/// Entry point for the Qt shell.  Called from main.cpp when ARROW_WITH_UI is
/// defined.  Returns the QApplication exit code.
///
/// @post The application's lifecycle has been shut down by the caller.
int run_shell(int argc, char** argv, const ShellInfo& info);

}  // namespace arrow::ui
