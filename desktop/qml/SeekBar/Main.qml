// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Seekbar with A-B repeat markers — spec §7.1 layer 5 (PRESENTATION),
// REQ-PLS-042.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property int currentPosition: 0
    property int duration: 0
    property int abLoopStart: -1
    property int abLoopEnd: -1

    signal seekRequested(int pos)
    signal setLoopA()
    signal setLoopB()
    signal clearLoop()

    height: 64

    // Slider — occupies the top ~40px
    Slider {
        id: seekSlider
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 40
        from: 0
        to: Math.max(1, root.duration)
        value: root.currentPosition

        // Click anywhere on the track to seek
        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
                if (root.duration > 0) {
                    const posMs = Math.round(mouse.x / parent.width * root.duration)
                    root.seekRequested(Math.max(0, Math.min(posMs, root.duration)))
                }
            }
        }

        // Handle: only visible on hover/press
        handle: Rectangle {
            width: 14
            height: 14
            radius: Theme.radius_full
            color: Theme.colors.primary
            visible: seekSlider.pressed || seekSlider.hovered
        }

        // Played fill (drawn in background)
        background: Item {}
    }

    // Played region overlay
    Rectangle {
        id: playedFill
        anchors {
            left: seekSlider.left
            right: seekSlider.right
            verticalCenter: seekSlider.verticalCenter
        }
        height: 4
        x: seekSlider.position * seekSlider.width
        width: seekSlider.width - x
        color: Theme.colors.primary
        radius: 2
    }

    // A-B loop region
    Rectangle {
        id: abRegion
        anchors {
            left: seekSlider.left
            right: seekSlider.right
            verticalCenter: seekSlider.verticalCenter
        }
        height: 4
        visible: root.abLoopStart >= 0 && root.abLoopEnd > root.abLoopStart

        leftPadding: root.duration > 0
                     ? (root.abLoopStart / root.duration) * seekSlider.width
                     : 0
        rightPadding: root.duration > 0
                      ? seekSlider.width - (root.abLoopEnd / root.duration) * seekSlider.width
                      : 0
        color: Theme.colors.primaryContainer
        opacity: 0.5
        radius: 2
    }

    // A marker
    Rectangle {
        anchors.verticalCenter: seekSlider.verticalCenter
        visible: root.abLoopStart >= 0
        height: seekSlider.height * 0.6
        width: 3
        x: root.duration > 0
           ? (root.abLoopStart / root.duration) * seekSlider.width - 1
           : 0
        color: Theme.colors.tertiary
        radius: 1.5
    }

    // B marker
    Rectangle {
        anchors.verticalCenter: seekSlider.verticalCenter
        visible: root.abLoopEnd > 0 && root.abLoopStart >= 0
        height: seekSlider.height * 0.6
        width: 3
        x: root.duration > 0
           ? (root.abLoopEnd / root.duration) * seekSlider.width - 1
           : 0
        color: Theme.colors.tertiary
        radius: 1.5
    }

    // ── A-B controls row ───────────────────────────────────────────────────
    Row {
        anchors {
            top: seekSlider.bottom
            left: parent.left
            right: parent.right
            topMargin: Theme.spacing_xs
        }
        height: 20
        spacing: Theme.spacing_sm

        Button {
            id: loopAButton
            text: "A"
            font.bold: root.abLoopStart >= 0
            onClicked: root.setLoopA()
            ToolTip.text: qsTranslate("ArrowPlayer", "Set loop start (A)")
            ToolTip.visible: hovered
        }

        Button {
            id: loopBButton
            text: "B"
            font.bold: root.abLoopEnd > 0
            enabled: root.abLoopStart >= 0
            onClicked: root.setLoopB()
            ToolTip.text: qsTranslate("ArrowPlayer", "Set loop end (B)")
            ToolTip.visible: hovered
        }

        Button {
            id: clearLoopButton
            text: "✕"
            enabled: root.abLoopStart >= 0
            onClicked: root.clearLoop()
            ToolTip.text: qsTranslate("ArrowPlayer", "Clear A–B loop")
            ToolTip.visible: hovered
        }
    }

    // ── Hover tooltip ──────────────────────────────────────────────────────
    Label {
        id: hoverTooltip
        visible: seekSlider.hovered && root.duration > 0
        text: formatTime(Math.round(seekSlider.position * root.duration))
        font.pixelSize: 12
        color: Theme.colors.onSurfaceSecondary
        x: seekSlider.handle.x - width / 2
        y: seekSlider.handle.y - height - Theme.spacing_xs
    }

    function formatTime(ms) {
        if (ms < 0) return "--:--";
        const totalSeconds = Math.floor(ms / 1000);
        const h = Math.floor(totalSeconds / 3600);
        const m = Math.floor((totalSeconds % 3600) / 60);
        const s = totalSeconds % 60;
        if (h > 0) {
            return "%1:%2:%3".arg(h).arg(m.toString().padStart(2, "0"))
                                 .arg(s.toString().padStart(2, "0"));
        }
        return "%1:%2".arg(m).arg(s.toString().padStart(2, "0"));
    }
}
