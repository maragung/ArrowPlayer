// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// The Phase 0 shell window — spec §7.1 layer 5 (PRESENTATION), §28 Phase 0
// exit gate 1 ("window opens") and exit gate 7 ("version string shown in
// About"). Everything here is presentation: it displays what it is handed
// (`ShellInfo`) and holds no business logic, per §7.1's "zero business logic
// in presentation".
//
// This header is public so the off-screen QTest suite can construct the
// window with `QT_QPA_PLATFORM=offscreen` and assert what About shows — a
// modal dialog that can only be opened by clicking is a UI that nobody has
// ever verified, and gate 7 is about what the user sees, not what the code
// contains.

#pragma once

#include <QWidget>

#include <string_view>

#include "arrow/ui/shell.hpp"

class QAction;

namespace arrow::ui {

/// The single Phase 0 main window.
///
/// Owns a menu bar with Help → About, and a small central widget that states
/// the build identity — deliberately modest, because §28 Phase 3 owns the real
/// UI. `info` is stored by value (three views and a bool, so copying is free)
/// so the About dialog can be re-shown at any time without holding a pointer
/// to something the caller owns.
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(const ShellInfo& info, QWidget* parent = nullptr);

    /// The text the About dialog shows, composed in one place.
    ///
    /// `showAboutDialog()` renders exactly this string; a test asserts on this
    /// instead of opening the modal. It is deliberately *not* a single
    /// sentence assembled from fragments: each line is a translated unit with
    /// a `%1` placeholder, which is what REQ-UIX-078 permits a translator to
    /// reorder.
    [[nodiscard]] QString aboutText() const;

public slots:
    /// Opens the About dialog as a modal. Keyboard-navigable and resizable
    /// where content warrants, as REQ-UIX-016 requires of every dialog.
    void showAboutDialog();

private:
    ShellInfo info_;
};

}  // namespace arrow::ui
