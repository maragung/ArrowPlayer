// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// The Phase 0 main window — spec §28 exit gate 1 ("window opens") and gate 7
// ("version string generated from git and shown in About", REQ-BLD-007).
//
// Layout is deliberately trivial: a menu bar carrying Help → About and a
// central label stating the build identity. §28 Phase 3 owns the real UI
// (library views, Now Playing, preferences); this window exists so that Phase
// 0's gates have something real to open and read.

#include "arrow/ui/main_window.hpp"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QString>
#include <QVBoxLayout>

namespace arrow::ui {

MainWindow::MainWindow(const ShellInfo& info, QWidget* parent) : QWidget(parent), info_(info) {
    setWindowTitle(tr("Arrow Player"));
    // A window that cannot be shrunk below its content is a window some
    // screen sizes cannot host; REQ-UIX-016 requires resizability where
    // content warrants.
    setMinimumSize(360, 120);

    auto* root = new QVBoxLayout(this);

    auto* menu_bar = new QMenuBar(this);
    QMenu* help_menu = menu_bar->addMenu(tr("&Help"));
    QAction* about_action =
        help_menu->addAction(tr("&About Arrow Player…"), this, &MainWindow::showAboutDialog);
    about_action->setMenuRole(QAction::AboutRole);
    root->setMenuBar(menu_bar);

    // The central statement of build identity. A label, not a splash: it is
    // the on-screen rendering of the same fields the About dialog shows.
    auto* identity = new QLabel(aboutText(), this);
    identity->setTextInteractionFlags(Qt::TextSelectableByMouse);
    identity->setWordWrap(true);
    root->addWidget(identity);
}

QString MainWindow::aboutText() const {
    const QString version = QString::fromUtf8(info_.version.data(),
                                              static_cast<qsizetype>(info_.version.size()));
    const QString sha = QString::fromUtf8(info_.git_sha.data(),
                                          static_cast<qsizetype>(info_.git_sha.size()));

    // Each line is one translated unit with a %1 placeholder — a translator
    // can reorder them, which REQ-UIX-078 requires and an assembled sentence
    // would forbid.
    QString text = tr("Arrow Player %1").arg(version);
    text += QLatin1Char('\n');
    text += tr("Commit: %1").arg(sha);
    if (info_.git_dirty) {
        text += QLatin1Char('\n');
        text += tr("Built from a working tree with uncommitted changes");
    }
    return text;
}

void MainWindow::showAboutDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About Arrow Player"));
    dialog.setMinimumWidth(360);

    auto* layout = new QVBoxLayout(&dialog);
    auto* details = new QLabel(aboutText(), &dialog);
    details->setTextFormat(Qt::PlainText);
    details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details->setWordWrap(true);
    layout->addWidget(details);

    // Close is the only action, so Enter and Escape both dismiss it and focus
    // never gets trapped — REQ-UIX-016's keyboard-navigable clause.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

}  // namespace arrow::ui
