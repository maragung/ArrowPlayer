// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Genre list view — spec §7.1 layer 5 (PRESENTATION).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: root

    signal itemActivated(int index)
    signal itemContextMenu(int index)

    model: genreModel
    delegate: genreDelegate
    spacing: Theme.spacing_xs

    Component {
        id: genreDelegate
        Rectangle {
            required property int index
            required property string genreName
            required property int trackCount

            width: ListView.view.width
            height: 48
            color: Theme.colors.surfaceContainerHighest
            radius: Theme.radius_sm

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing_md
                anchors.rightMargin: Theme.spacing_md
                spacing: Theme.spacing_md

                Label {
                    text: genreName
                    font.pixelSize: 14
                    color: Theme.colors.onSurface
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: qsTranslate("ArrowPlayer", "%1 songs").arg(trackCount)
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.itemContextMenu(genreDelegate.index)
                    } else {
                        root.itemActivated(genreDelegate.index)
                    }
                }
            }
        }
    }

    Menu {
        id: genreContextMenu
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Play All")
            onTriggered: root.itemActivated(genreContextMenu._index)
        }
        MenuItem {
            text: qsTranslate("ArrowPlayer", "Add to Queue")
            onTriggered: root.itemContextMenu(genreContextMenu._index)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) {
            const idx = indexAt(mouse.x, mouse.y)
            genreContextMenu._index = idx
            genreContextMenu.popup()
        }
    }
}
