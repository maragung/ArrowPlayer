// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Settings dialog — spec §7.1 layer 5 (PRESENTATION), §12.4.
//
// Tabbed settings window backed by the application DataStore.
// All changes are written to DataStore immediately (no Apply/Save button).

#pragma once

#include <QDialog>
#include <QPointer>

class QTabWidget;
class QString;

namespace arrow::ui {

/// The tabbed settings/preferences dialog.
///
/// Backed by the application DataStore.  Each tab is a separate widget created
/// once and deleted with the dialog.
class SettingsDialog final : public QDialog {
    Q_OBJECT

  public:
    /// Constructs the dialog as a top-level window.
    ///
    /// @param parent  The widget to centre over.  nullptr means the dialog is
    ///                centred on the screen.
    explicit SettingsDialog(QWidget* parent = nullptr);

    ~SettingsDialog() override;

    // non-copyable, non-movable
    SettingsDialog(const SettingsDialog&) = delete;
    SettingsDialog& operator=(const SettingsDialog&) = delete;
    SettingsDialog(SettingsDialog&&) = delete;
    SettingsDialog& operator=(SettingsDialog&&) = delete;

  public slots:
    /// Reinitialises every tab from the current DataStore values and shows the
    /// dialog.  Call this instead of QDialog::open() when the dialog may have
    /// been showing stale values.
    void refreshAndShow();

  private:
    QPointer<QTabWidget> tab_widget_;
};

}  // namespace arrow::ui
