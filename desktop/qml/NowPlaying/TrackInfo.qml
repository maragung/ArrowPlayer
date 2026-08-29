// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Track info block — spec §7.1 layer 5 (PRESENTATION), §12.1.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string artist:    ""
    property string title:     ""
    property string album:     ""
    property string year:      ""
    property int    duration:  0   // seconds

    spacing: Theme.spacing_xs
    anchors.horizontalCenter: parent.horizontalCenter

    // Track title — prominent, single line with ellipsis
    Label {
        id: titleLabel
        text: root.title || qsTranslate("ArrowPlayer", "No track")
        font.pixelSize: 24
        font.weight: Font.DemiBold
        color: root.title ? Theme.colors.onSurface
                          : Theme.colors.onSurfaceSecondary
        elide: Text.ElideRight
        maximumLineCount: 1
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
    }

    // Artist — secondary
    Label {
        id: artistLabel
        text: root.artist
        font.pixelSize: 18
        font.weight: Font.DemiBold
        color: Theme.colors.onSurfaceSecondary
        elide: Text.ElideRight
        maximumLineCount: 1
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        visible: root.artist !== ""
    }

    // Album + year
    RowLayout {
        id: albumRow
        spacing: Theme.spacing_xs
        Layout.alignment: Qt.AlignHCenter
        visible: root.album !== ""

        Label {
            text: root.album
            font.pixelSize: 14
            color: Theme.colors.onSurfaceSecondary
            elide: Text.ElideRight
        }

        Label {
            text: root.year ? "(%1)".arg(root.year) : ""
            font.pixelSize: 14
            color: Theme.colors.onSurfaceSecondary
            visible: root.year !== ""
        }
    }

    // Duration
    Label {
        id: durationLabel
        text: root.duration > 0 ? formatDuration(root.duration)
                               : ""
        font.pixelSize: 12
        font.weight: Font.Normal
        color: Theme.colors.onSurfaceTertiary
        Layout.alignment: Qt.AlignHCenter
        visible: root.duration > 0
    }

    function formatDuration(seconds) {
        if (!seconds) return "";
        const m = Math.floor(seconds / 60);
        const s = Math.floor(seconds % 60);
        return "%1:%2".arg(m).arg(s.toString().padStart(2, "0"));
    }
}
