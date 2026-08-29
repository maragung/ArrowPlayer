// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Artist list view — spec §7.1 layer 5 (PRESENTATION).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: root

    signal itemActivated(int index)
    signal itemContextMenu(int index)

    model: artistModel
    delegate: artistDelegate
    spacing: Theme.spacing_xs

    Component {
        id: artistDelegate
        Rectangle {
            required property int index
            required property string artistName
            required property int albumCount
            required property int trackCount

            width: ListView.view.width
            height: 52
            color: ListView.isCurrentItem
                   ? Theme.colors.surfaceContainerHighest
                   : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing_md
                anchors.rightMargin: Theme.spacing_md
                spacing: Theme.spacing_md

                // Artist initial avatar
                Rectangle {
                    width: 36
                    height: 36
                    radius: Theme.radius_sm
                    color: Theme.colors.surfaceContainerHigh
                    Layout.alignment: Qt.AlignVCenter

                    Label {
                        anchors.centerIn: parent
                        text: artistName.charAt(0).toUpperCase()
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: Theme.colors.onSurfaceVariant
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: artistName
                        font.pixelSize: 14
                        color: Theme.colors.onSurface
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTranslate("ArrowPlayer", "%1 albums, %2 songs")
                            .arg(albumCount).arg(trackCount)
                        font.pixelSize: 12
                        color: Theme.colors.onSurfaceSecondary
                    }
                }

                ToolButton {
                    icon.source: "qrc:/qml/Theme/icon-play.svg"
                    onClicked: root.itemActivated(artistDelegate.index)
                    ToolTip.text: qsTranslate("ArrowPlayer", "Play all")
                    ToolTip.visible: hovered
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        root.itemContextMenu(artistDelegate.index)
                    } else {
                        root.itemActivated(artistDelegate.index)
                    }
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: root.itemContextMenu(indexAt(mouse.x, mouse.y))
    }
}
