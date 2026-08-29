// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Library browser — spec §7.1 layer 5 (PRESENTATION), §12.1, REQ-LIB-061.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "LibraryRoot"

    // ── Category tab bar ───────────────────────────────────────────────────
    Rectangle {
        id: categoryBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: Theme.colors.surfaceContainerHighest

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Repeater {
                model: [
                    qsTranslate("ArrowPlayer", "Artists"),
                    qsTranslate("ArrowPlayer", "Albums"),
                    qsTranslate("ArrowPlayer", "Songs"),
                    qsTranslate("ArrowPlayer", "Genres"),
                    qsTranslate("ArrowPlayer", "Playlists")
                ]
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    Layout.preferredHeight: parent.height
                    color: categoryTabIndex === index
                           ? Theme.colors.surface
                           : "transparent"
                    border.width: categoryTabIndex === index ? 2 : 0
                    border.color: Theme.colors.primary

                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: 14
                        font.weight: categoryTabIndex === index
                                     ? Font.DemiBold : Font.Normal
                        color: categoryTabIndex === index
                               ? Theme.colors.primary
                               : Theme.colors.onSurfaceVariant
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: categoryTabIndex = index
                    }
                }
            }
        }
    }

    // ── Toolbar ───────────────────────────────────────────────────────────
    Rectangle {
        id: toolBar
        anchors.top: categoryBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: Theme.colors.surfaceContainerHighest

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing_md
            anchors.rightMargin: Theme.spacing_md
            spacing: Theme.spacing_sm

            Label {
                text: qsTranslate("ArrowPlayer", "Search:")
                font.pixelSize: 13
                Layout.alignment: Qt.AlignVCenter
            }

            TextField {
                id: searchField
                Layout.preferredWidth: 280
                placeholderText: qsTranslate("ArrowPlayer", "Search library…")
                onTextChanged: searchTimer.restart()
            }

            Timer {
                id: searchTimer
                interval: 120
                onTriggered: root.performSearchRequested(searchField.text)
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                icon.source: viewMode === "grid"
                             ? "qrc:/qml/Theme/icon-list.svg"
                             : "qrc:/qml/Theme/icon-grid.svg"
                checked: viewMode === "grid"
                checkable: true
                onClicked: viewMode = viewMode === "grid" ? "list" : "grid"
                ToolTip.text: qsTranslate("ArrowPlayer", "Toggle grid / list view")
                ToolTip.visible: hovered
            }

            ToolButton {
                icon.source: "qrc:/qml/Theme/icon-sort.svg"
                onClicked: sortMenu.popup()
                ToolTip.text: qsTranslate("ArrowPlayer", "Sort")
                ToolTip.visible: hovered

                Menu {
                    id: sortMenu
                    MenuItem {
                        text: qsTranslate("ArrowPlayer", "Sort by Name")
                        onTriggered: root.setSortRequested("name")
                    }
                    MenuItem {
                        text: qsTranslate("ArrowPlayer", "Sort by Artist")
                        onTriggered: root.setSortRequested("artist")
                    }
                    MenuItem {
                        text: qsTranslate("ArrowPlayer", "Sort by Date Added")
                        onTriggered: root.setSortRequested("dateAdded")
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTranslate("ArrowPlayer", "Sort Ascending")
                        onTriggered: root.setSortDirectionRequested(Qt.AscendingOrder)
                    }
                    MenuItem {
                        text: qsTranslate("ArrowPlayer", "Sort Descending")
                        onTriggered: root.setSortDirectionRequested(Qt.DescendingOrder)
                    }
                }
            }
        }
    }

    // ── Content area ────────────────────────────────────────────────────────
    StackLayout {
        id: contentStack
        anchors.top: toolBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        currentIndex: categoryTabIndex

        ArtistListView {
            id: artistList
            visible: true
            onItemActivated: function(idx) { root.activateArtistRequested(idx) }
            onItemContextMenu: function(idx) { root.showItemContextMenuRequested(idx, "artist") }
        }

        AlbumListView {
            id: albumList
            visible: true
            viewMode: root.viewMode
            onItemActivated: function(idx) { root.activateAlbumRequested(idx) }
            onItemContextMenu: function(idx) { root.showItemContextMenuRequested(idx, "album") }
        }

        SongListView {
            id: songList
            visible: true
            viewMode: root.viewMode
            onItemActivated: function(idx) { root.activateSongRequested(idx) }
            onItemContextMenu: function(idx) { root.showItemContextMenuRequested(idx, "song") }
        }

        GenreListView {
            id: genreList
            visible: true
            onItemActivated: function(idx) { root.activateGenreRequested(idx) }
            onItemContextMenu: function(idx) { root.showItemContextMenuRequested(idx, "genre") }
        }

        PlaylistListView {
            id: playlistList
            visible: true
            onItemActivated: function(idx) { root.activatePlaylistRequested(idx) }
            onItemContextMenu: function(idx) { root.showItemContextMenuRequested(idx, "playlist") }
        }
    }

    // ── Empty state ────────────────────────────────────────────────────────
    Label {
        anchors.centerIn: contentStack
        text: qsTranslate("ArrowPlayer",
                          "No items in library.\nAdd music to get started.")
        font.pixelSize: 18
        font.weight: Font.DemiBold
        color: Theme.colors.onSurfaceSecondary
        visible: isLibraryEmpty
    }

    // ── Properties ─────────────────────────────────────────────────────────
    property string viewMode: "grid"
    property bool isLibraryEmpty: false
    property int categoryTabIndex: 0

    // ── Signals ─────────────────────────────────────────────────────────────
    signal performSearchRequested(string query)
    signal setSortRequested(string field)
    signal setSortDirectionRequested(int direction)

    signal activateArtistRequested(int index)
    signal activateAlbumRequested(int index)
    signal activateSongRequested(int index)
    signal activateGenreRequested(int index)
    signal activatePlaylistRequested(int index)

    signal showItemContextMenuRequested(int itemIndex, string itemType)

    signal playItemRequested(int itemIndex, string itemType)
    signal playNextItemRequested(int itemIndex, string itemType)
    signal addToQueueItemRequested(int itemIndex, string itemType)
    signal addToPlaylistItemRequested(int itemIndex, string itemType)
    signal showPropertiesItemRequested(int itemIndex, string itemType)
}
