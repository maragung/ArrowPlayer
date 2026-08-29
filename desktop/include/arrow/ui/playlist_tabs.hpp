// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Playlist tab strip — spec §7.1 layer 5 (PRESENTATION), REQ-PLS-020.
//
// Qt Widgets tab bar that manages multiple open playlists.  Supports drag-to-
// reorder, a close button on each tab, and a "+" button to create a new
// playlist.  The tab bar owns the list of open playlist IDs; callers use
// the signals to drive their own models.

#pragma once

#include <QTabWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMouseEvent;

namespace arrow::ui {

/// The playlist tab strip widget.
///
/// Emits signals when tabs are added, removed, reordered, closed, or
/// activated.  The owner is responsible for updating the playlist content
/// area in response to currentChanged.
class PlaylistTabs final : public QTabWidget {
    Q_OBJECT

    /// Emitted when the user clicks the "+" button or presses the keyboard
    /// shortcut for "new playlist".
    Q_PROPERTY(Qt::TabCloseButtonVisibility tabCloseButtonVisible
                   READ tabCloseButtonVisible WRITE setTabCloseButtonVisible)

  public:
    /// Constructs an empty playlist tab strip.
    ///
    /// @param parent  Passed to QTabWidget.
    explicit PlaylistTabs(QWidget* parent = nullptr);

    ~PlaylistTabs() override;

    // non-copyable, non-movable
    PlaylistTabs(const PlaylistTabs&) = delete;
    PlaylistTabs& operator=(const PlaylistTabs&) = delete;
    PlaylistTabs(PlaylistTabs&&) = delete;
    PlaylistTabs& operator=(PlaylistTabs&&) = delete;

    // Re-implemented to intercept tab changes and emit currentPlaylistChanged.
    int currentIndex() const override;
    void setCurrentIndex(int index) override;

  public slots:
    /// Adds a playlist tab and optionally activates it.
    ///
    /// @param id      Unique playlist identifier (exposed via playlistId().
    /// @param title   Initial tab title (translatable string).
    /// @param select  If true, the new tab becomes the current tab.
    void addPlaylist(int64_t id, const QString& title, bool select = false);

    /// Removes a playlist tab by its identifier.  No-op if the tab does not
    /// exist.
    void removePlaylist(int64_t id);

    /// Updates the title of a playlist tab.
    void setPlaylistTitle(int64_t id, const QString& title);

    /// Moves a tab from \a fromIndex to \a toIndex.
    void moveTab(int fromIndex, int toIndex);

  signals:
    /// Emitted after a tab is added.
    void playlistAdded(int64_t id);

    /// Emitted after the tab is removed.  The model backing the content area
    /// should be cleared if this was the current tab.
    void playlistRemoved(int64_t id);

    /// Emitted after the user reorders tabs via drag-and-drop.
    void playlistMoved(int64_t id, int fromIndex, int toIndex);

    /// Emitted when the current playlist changes.
    void currentPlaylistChanged(int64_t id);

    /// Emitted when the user clicks the "+" button.
    void newPlaylistRequested();

    /// Emitted when the user clicks the close button on a tab.
    void closePlaylistRequested(int64_t id);

  protected:
    // Re-implemented to handle drag-and-drop reordering.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // Re-implemented to emit closePlaylistRequested for the tab under the
    // close button.
    void tabCloseRequested(int index);

    /// Returns the playlist ID for \a index, or -1 if invalid.
    [[nodiscard]] int64_t playlistId(int index) const;

    /// Maps playlist ID → tab index.  Returns -1 if not found.
    [[nodiscard]] int indexOf(int64_t id) const;

  private:
    struct TabData {
        int64_t id;
    };
    QList<TabData> tab_data_;  // parallel to QTabWidget::count()
};

}  // namespace arrow::ui
