// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Album art display — spec §7.1 layer 5 (PRESENTATION), §12.1.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Image {
    id: root

    /// The artwork URL (file:// or qrc://). Empty string shows the placeholder.
    property string artwork: ""

    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true   // high-quality downscaling

    source: artwork || "qrc:/qml/Theme/placeholder-album.svg"
    asynchronous: true   // do not block the UI thread

    // Placeholder: gradient background with geometric music note
    Rectangle {
        anchors.fill: parent
        color: Theme.colors.surfaceVariant
        visible: root.status === Image.Loading || root.status === Image.Error

        Item {
            anchors.centerIn: parent
            width: parent.width * 0.4
            height: parent.height * 0.4

            Rectangle {
                x: parent.width * 0.35
                y: parent.height * 0.1
                width: parent.width * 0.08
                height: parent.height * 0.55
                color: Theme.colors.onSurfaceVariant
                radius: width / 2
            }
            Rectangle {
                x: parent.width * 0.25
                y: parent.height * 0.35
                width: parent.width * 0.35
                height: parent.height * 0.1
                color: Theme.colors.onSurfaceVariant
                radius: height / 2
            }
            Rectangle {
                x: parent.width * 0.55
                y: parent.height * 0.2
                width: parent.width * 0.08
                height: parent.height * 0.45
                color: Theme.colors.onSurfaceVariant
                radius: width / 2
            }
            Rectangle {
                x: parent.width * 0.45
                y: parent.height * 0.4
                width: parent.width * 0.25
                height: parent.height * 0.1
                color: Theme.colors.onSurfaceVariant
                radius: height / 2
            }
        }
    }

    // Rounded corner overlay
    border.color: Theme.colors.outline
    border.width: 1
    radius: Theme.radius_md

    // Cross-fade when artwork changes
    Behavior on source {
        NumberAnimation {
            property: "opacity"
            from: 0.6
            to: 1.0
            duration: Theme.duration("fast")
            easing.type: Easing.OutCubic
        }
    }

    // -----------------------------------------------------------------------
    // Context menu on right-click
    // -----------------------------------------------------------------------
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: contextMenu.popup()

        Menu {
            id: contextMenu
            MenuItem {
                text: qsTranslate("ArrowPlayer", "Show Album Info")
                onTriggered: root.showAlbumInfoRequested()
            }
            MenuItem {
                text: qsTranslate("ArrowPlayer", "Search Cover Art")
                onTriggered: root.searchCoverArtRequested()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTranslate("ArrowPlayer", "Save Cover Art…")
                onTriggered: root.saveCoverArtRequested()
            }
        }
    }

    signal showAlbumInfoRequested()
    signal searchCoverArtRequested()
    signal saveCoverArtRequested()
}
