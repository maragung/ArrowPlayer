// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Playlist tab strip — spec §7.1 layer 5 (PRESENTATION), REQ-PLS-020.

#include "arrow/ui/playlist_tabs.hpp"

#include <QAction>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionTab>
#include <QToolButton>

#include <algorithm>

namespace {

/// Small helper: maps a pixel position to a tab index.
// Returns count() if dropped to the right of the last tab.
int indexAtPos(const QTabWidget& tw, const QPoint& pos) {
    for (int i = 0; i < tw.count(); ++i) {
        if (tw.tabBar()->tabRect(i).contains(pos)) {
            return i;
        }
    }
    return tw.count();  // insert at end
}

}  // namespace

namespace arrow::ui {

PlaylistTabs::PlaylistTabs(QWidget* parent)
    : QTabWidget(parent) {
    setMovable(true);          // Qt handles the visual drag; we handle signals
    setTabsClosable(true);     // close button on every tab
    setDocumentMode(true);    // borderless on platforms that use it

    // "+" button on the far right of the tab bar, before the window's native
    // right-edge buttons on macOS.
    QToolButton* add_button = new QToolButton(this);
    add_button->setText(tr("+"));
    add_button->setFocusPolicy(Qt::NoFocus);
    add_button->setCursor(Qt::PointingHandCursor);
    connect(add_button, &QToolButton::clicked, this, &PlaylistTabs::newPlaylistRequested);
    setCornerWidget(add_button, Qt::TopRightCorner);

    connect(this, &QTabWidget::tabCloseRequested, this, &PlaylistTabs::tabCloseRequested);
}

PlaylistTabs::~PlaylistTabs() = default;

int PlaylistTabs::currentIndex() const {
    return QTabWidget::currentIndex();
}

void PlaylistTabs::setCurrentIndex(int index) {
    QTabWidget::setCurrentIndex(index);
    if (index >= 0 && index < tab_data_.size()) {
        emit currentPlaylistChanged(tab_data_[index].id);
    }
}

void PlaylistTabs::addPlaylist(int64_t id, const QString& title, bool select) {
    const int idx = indexOf(id);
    if (idx >= 0) {
        // Already open — just raise it.
        QTabWidget::setCurrentIndex(idx);
        return;
    }

    const int new_index = count();
    tab_data_.append({id});
    QTabWidget::addTab(new QLabel(this), title);

    if (select) {
        QTabWidget::setCurrentIndex(new_index);
    }

    emit playlistAdded(id);
}

void PlaylistTabs::removePlaylist(int64_t id) {
    const int idx = indexOf(id);
    if (idx < 0) return;
    tab_data_.removeAt(idx);
    removeTab(idx);
    emit playlistRemoved(id);
}

void PlaylistTabs::setPlaylistTitle(int64_t id, const QString& title) {
    const int idx = indexOf(id);
    if (idx >= 0) {
        QTabWidget::setTabText(idx, title);
    }
}

void PlaylistTabs::moveTab(int fromIndex, int toIndex) {
    if (fromIndex == toIndex) return;
    if (fromIndex < 0 || fromIndex >= tab_data_.size()) return;
    if (toIndex < 0 || toIndex > tab_data_.size()) return;

    tab_data_.move(fromIndex, toIndex);
    QTabWidget::insertTab(toIndex, widget(fromIndex), tabBar()->tabText(fromIndex));
    // removeTab(fromIndex) shifts indices; re-do it
    QTabWidget::removeTab(fromIndex < toIndex ? toIndex : fromIndex);
    // Restore selection
    QTabWidget::setCurrentIndex(toIndex);

    emit playlistMoved(tab_data_[toIndex].id, fromIndex, toIndex);
}

void PlaylistTabs::dragEnterEvent(QDragEnterEvent* event) {
    if (event->source() == tabBar()) {
        event->acceptProposedAction();
    }
}

void PlaylistTabs::dragMoveEvent(QDragMoveEvent* event) {
    if (event->source() == tabBar()) {
        event->acceptProposedAction();
    }
}

void PlaylistTabs::dropEvent(QDropEvent* event) {
    if (event->source() != tabBar()) return;

    // Qt's internal drag does not carry our index; we detect the drop
    // position from the event location and swap the current tab there.
    const QPoint global_pos = tabBar()->mapToGlobal(event->position().toPoint());
    const QPoint local_pos = tabBar()->mapFromGlobal(global_pos);
    const int from = currentIndex();
    const int to = indexAtPos(*this, local_pos);

    if (from >= 0 && to >= 0 && from != to) {
        moveTab(from, to);
    }
    event->acceptProposedAction();
}

void PlaylistTabs::tabCloseRequested(int index) {
    if (index < 0 || index >= tab_data_.size()) return;
    const int64_t id = tab_data_[index].id;
    emit closePlaylistRequested(id);
}

int64_t PlaylistTabs::playlistId(int index) const {
    if (index < 0 || index >= tab_data_.size()) return -1;
    return tab_data_[index].id;
}

int PlaylistTabs::indexOf(int64_t id) const {
    for (int i = 0; i < tab_data_.size(); ++i) {
        if (tab_data_[i].id == id) return i;
    }
    return -1;
}

}  // namespace arrow::ui
