// SPDX-License-Identifier: MPL-2.0
// The desktop shell entry point — spec §7.1 layer 5 (PRESENTATION), §28 Phase 0
// (the "hello window" and exit gate 7, "version string shown in About").
//
// This header is the seam between layer 4 (APPLICATION, `main.cpp` + the DI
// bootstrap) and layer 5 (this Qt Widgets shell). It is deliberately tiny and
// Qt-free: `main.cpp` includes it, fills a `ShellInfo`, and hands control over.
// Keeping Qt out of this one header means the application layer never has to
// see a Qt type to launch the UI, which is what §7.1's "zero business logic in
// presentation, and presentation reachable only downward" costs to honour here.
//
// The version fields are passed in rather than read here on purpose: the
// generated `arrow/version.hpp` (REQ-BLD-007) is produced by CMake from git,
// and exit gate 7 requires the value shown in About to be *that* generated
// value, never a literal baked into the UI. The shell therefore displays what
// it is handed and has no opinion about where the numbers came from.

#pragma once

#include <string_view>

namespace arrow::ui {

/// Version and build provenance the shell displays in the About dialog.
///
/// `main.cpp` fills exactly these three fields from the generated
/// `arrow::version` header (REQ-BLD-007):
///   version   = arrow::version::kString
///   git_sha   = arrow::version::kGitSha
///   git_dirty = arrow::version::kGitDirty != 0
///
/// The struct is intentionally not widened beyond what exit gate 7 needs; the
/// About dialog grows fields only when a requirement asks for them, so an
/// unverifiable UI stays as small as Phase 0 requires.
struct ShellInfo {
    std::string_view version;    ///< arrow::version::kString  (e.g. "0.1.0")
    std::string_view git_sha;    ///< arrow::version::kGitSha  (short hash)
    bool             git_dirty;  ///< arrow::version::kGitDirty != 0
};

/// Creates the QApplication, installs the locale translators, shows the main
/// window, and runs the Qt event loop until the last window closes. Returns the
/// event loop's exit code, which `main.cpp` returns from `main`.
///
/// Thread-safety: **main thread only** (§1.3 rule 4). This constructs a
/// `QApplication`, which Qt requires to live on the thread that runs `main`,
/// and then blocks on the event loop for the lifetime of the process. It must
/// be called exactly once, from `main`, and never from a worker thread. Not
/// RT-safe and never reachable from the audio callback (§7.3): it allocates,
/// blocks, and does file I/O through Qt's resource system.
///
/// `argc`/`argv` are forwarded to `QApplication` so Qt's own command-line
/// switches (`-style`, `-platform`, `-widgetcount`, …) keep working; Arrow's
/// own CLI parsing happens in layer 4 before this is called.
int run_shell(int argc, char** argv, const ShellInfo& info);

}  // namespace arrow::ui
