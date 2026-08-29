// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Playlist list view — spec §7.1 layer 5 (PRESENTATION), REQ-PLS-020.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: root

    signal itemActivated(int index)
    signal itemContextMenu(int index)
    signal newPlaylistRequested()
    signal renamePlaylistRequested(int index)
    signal deletePlaylistRequested(int index)
    signal showPlaylistPropertiesRequested(int index)

    model: playlistModel
    delegate: playlistDelegate
    spacing: Theme.spacing_xs

    // New playlist button at the top
    header: Rectangle {
        width: ListView.view.width
        height: 44
        color: Theme.colors.surface

        Button {
            anchors.centerIn: parent
            text: qsTranslate("ArrowPlayer", "+ New Playlist")
            onClicked: root.newPlaylistRequested()
        }
    }

    Component {
        id: playlistDelegate
        Rectangle {
            required property int index
            required property string playlistName
            required property int trackCount
            required property string artworkUrl

            width: ListView.view.width
            height: 56
            color: Theme.colors.surfaceContainerHighest
            radius: Theme.radius_sm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing_md
                anchors.rightMargin: Theme.spacing_md
                spacing: Theme.spacing_md

                Rectangle {
                    width: 40
                    height: 40
                    radius: Theme.radius_sm
                    color: Theme.colors.surfaceContainerHigh
                    Layout.alignment: Qt.AlignVCenter

                    Image {
                        anchors.fill: parent
                        source: artworkUrl
                        fillMode: Image.PreserveAspectCrop
                        visible: artworkUrl !== ""
                    }

                    Label {
                        anchors.centerIn: parent
                        text: "≡"
                        font.pixelSize: 18
                        color: Theme.colors.onSurfaceVariant
                        visible: artworkUrl === ""
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: playlistName
                        font.pixelSize: 14
                        color: Theme.colors.onSurface
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTranslate("ArrowPlayer", "%1 tracks").arg(trackCount)
                        font.pixelSize: 12
                        color: Theme.colors.onSurfaceSecondary
                    }
                }

                ToolButton {
                    icon.source: "qrc:/qml/Theme/icon-play.svg"
                    onClicked: root.itemActivated(playlistDelegate.index)
                    ToolTip.text: qsTranslate("ArrowPlayer", "Play playlist")
                    ToolTip.visible: hovered
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.itemContextMenu(playlistDelegate.index)
                    } else {
                        root.itemActivated(playlistDelegate.index)
                    }
                }
            }
        }
    }

    Menu {
        id: playlistContextMenu
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Rename")
            onTriggered: root.renamePlaylistRequested(playlistContextMenu._index)
        }
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Delete")
            onTriggered: root.deletePlaylistRequested(playlistContextMenu._index)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Properties")
            onTriggered: root.showPlaylistPropertiesRequested(playlistContextMenu._index)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) {
            const idx = indexAt(mouse.x, mouse.y)
            playlistContextMenu._index = idx
            playlistContextMenu.popup()
        }
    }
}
