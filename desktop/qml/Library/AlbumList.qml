// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Album list/grid view — spec §7.1 layer 5 (PRESENTATION).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: root

    property string viewMode: "grid"
    signal itemActivated(int index)
    signal itemContextMenu(int index)

    model: albumModel
    cellWidth: root.viewMode === "grid"
               ? Math.max(160, root.width / Math.max(1, Math.floor(root.width / 180)))
               : root.width
    cellHeight: root.viewMode === "grid" ? 220 : 64
    delegate: albumDelegate

    Menu {
        id: albumContextMenu
        property int _index: -1
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Play")
            onTriggered: root.itemActivated(albumContextMenu._index)
        }
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Add to Queue")
            onTriggered: root.addToQueueRequested(albumContextMenu._index)
        }
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Add to Playlist…")
            onTriggered: root.addToPlaylistRequested(albumContextMenu._index)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Properties")
            onTriggered: root.showPropertiesRequested(albumContextMenu._index)
        }
    }

    Component {
        id: albumDelegate
        Item {
            required property int index
            required property string albumTitle
            required property string artistName
            required property string year
            required property string artworkUrl

            width: GridView.view.cellWidth
            height: GridView.view.cellHeight

            // Album artwork
            Rectangle {
                id: artBox
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacing_sm
                anchors.rightMargin: Theme.spacing_sm
                anchors.topMargin: Theme.spacing_sm
                height: root.viewMode === "grid"
                        ? width - Theme.spacing_lg
                        : parent.height - Theme.spacing_md
                color: Theme.colors.surfaceContainerHigh
                radius: Theme.radius_md

                Image {
                    anchors.fill: parent
                    source: artworkUrl
                    fillMode: Image.PreserveAspectCrop
                    mipmap: true
                    visible: artworkUrl !== ""
                }

                // Year badge
                Label {
                    anchors {
                        bottom: parent.bottom
                        right: parent.right
                        margins: Theme.spacing_xs
                    }
                    text: year
                    font.pixelSize: 11
                    color: Theme.colors.onPrimary
                    padding: 2
                    background: Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        color: Theme.colors.primary
                        radius: Theme.radius_sm
                    }
                    visible: root.viewMode === "grid" && year !== ""
                }
            }

            // Album info
            ColumnLayout {
                anchors {
                    top: root.viewMode === "grid" ? artBox.bottom : parent.top
                    left: parent.left
                    right: parent.right
                    topMargin: Theme.spacing_xs
                    leftMargin: Theme.spacing_sm
                    rightMargin: Theme.spacing_sm
                }
                height: root.viewMode === "grid" ? implicitHeight : parent.height
                spacing: 2

                Label {
                    text: albumTitle
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    color: Theme.colors.onSurface
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    Layout.fillWidth: true
                }
                Label {
                    text: artistName
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    Layout.fillWidth: true
                    visible: root.viewMode === "grid"
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        albumContextMenu._index = albumDelegate.index
                        albumContextMenu.popup()
                    } else {
                        root.itemActivated(albumDelegate.index)
                    }
                }
            }
        }
    }

    signal addToQueueRequested(int index)
    signal addToPlaylistRequested(int index)
    signal showPropertiesRequested(int index)
}
