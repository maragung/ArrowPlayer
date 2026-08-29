// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Queue panel — spec §7.1 layer 5 (PRESENTATION), REQ-PLS-021.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "QueueRoot"

    // ── Header ──────────────────────────────────────────────────────────────
    Rectangle {
        id: headerBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 48
        color: Theme.colors.surfaceContainerHighest

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing_lg
            anchors.rightMargin: Theme.spacing_lg

            Label {
                text: qsTranslate("ArrowPlayer", "Queue")
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.alignment: Qt.AlignVCenter
            }

            Label {
                text: qsTranslate("ArrowPlayer", "%1 track(s)").arg(queueCount)
                font.pixelSize: 12
                color: Theme.colors.onSurfaceSecondary
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: Theme.spacing_sm
            }

            Item { Layout.fillWidth: true }

            Button {
                id: clearQueueButton
                text: qsTranslate("ArrowPlayer", "Clear")
                enabled: queueCount > 0
                onClicked: root.clearQueueRequested()
            }

            Button {
                id: saveAsPlaylistButton
                text: qsTranslate("ArrowPlayer", "Save as Playlist…")
                enabled: queueCount > 0
                onClicked: root.saveAsPlaylistRequested()
            }
        }
    }

    // ── Queue list ───────────────────────────────────────────────────────
    ListView {
        id: queueListView
        anchors.top: headerBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        model: queueModel
        delegate: queueItemDelegate
        spacing: Theme.spacing_xs

        // Drag-and-drop reordering
        drag.enabled: true
        drag.rows: true

        // Highlight current playing track
        highlight: Rectangle {
            color: Theme.colors.primaryContainer
            radius: Theme.radius_sm
        }
        highlightFollowsCurrentItem: true

        footer: Item {
            height: Theme.spacing_xl
        }

        // Empty state
        Label {
            anchors.centerIn: parent
            text: qsTranslate("ArrowPlayer",
                              "The queue is empty.\nDrag tracks here or use \"Play Next\".")
            font.pixelSize: 14
            color: Theme.colors.onSurfaceSecondary
            visible: queueCount === 0
        }
    }

    // ── Properties ─────────────────────────────────────────────────────────
    property int queueCount: queueModel ? queueModel.count : 0

    // ── Signals ───────────────────────────────────────────────────────────
    signal moveTrackRequested(int fromIndex, int toIndex)
    signal removeTrackRequested(int index)
    signal clearQueueRequested()
    signal saveAsPlaylistRequested()
    signal playTrackRequested(int index)

    // ── Item delegate ─────────────────────────────────────────────────────
    Component {
        id: queueItemDelegate
        Rectangle {
            id: itemRoot
            required property int index
            required property string title
            required property string artist
            required property string album
            required property int duration
            required property bool isCurrentTrack

            width: ListView.view.width
            height: 52
            color: isCurrentTrack
                   ? Theme.colors.primaryContainer
                   : Theme.colors.surfaceContainerHighest
            radius: Theme.radius_sm

            Drag.active: dragArea.drag.active
            Drag.dragType: Drag.Internal

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing_md
                anchors.rightMargin: Theme.spacing_md
                spacing: Theme.spacing_md

                // Drag handle
                Label {
                    text: "≡"
                    font.pixelSize: 18
                    color: Theme.colors.onSurfaceVariant
                    Layout.alignment: Qt.AlignVCenter
                    visible: !isCurrentTrack
                }

                // Track number
                Label {
                    text: (index + 1).toString()
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                    Layout.preferredWidth: 32
                    Layout.alignment: Qt.AlignVCenter
                }

                // Track info
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: title || qsTranslate("ArrowPlayer", "Unknown")
                        font.pixelSize: 14
                        color: isCurrentTrack
                               ? Theme.colors.onPrimaryContainer
                               : Theme.colors.onSurface
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }
                    Label {
                        text: artist || ""
                        font.pixelSize: 12
                        color: Theme.colors.onSurfaceSecondary
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }
                }

                // Duration
                Label {
                    text: formatDuration(duration)
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                    Layout.alignment: Qt.AlignVCenter
                }

                // Remove button
                ToolButton {
                    text: "✕"
                    onClicked: root.removeTrackRequested(itemRoot.index)
                    ToolTip.text: qsTranslate("ArrowPlayer", "Remove from queue")
                    ToolTip.visible: hovered
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                drag.target: itemRoot
                onClicked: root.playTrackRequested(itemRoot.index)
            }
        }
    }

    function formatDuration(seconds) {
        if (!seconds) return "--:--";
        const m = Math.floor(seconds / 60);
        const s = Math.floor(seconds % 60);
        return "%1:%2".arg(m).arg(s.toString().padStart(2, "0"));
    }
}
