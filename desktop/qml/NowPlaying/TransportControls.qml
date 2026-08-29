// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Transport controls — spec §7.1 layer 5 (PRESENTATION), §12.1.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    property bool isPlaying:      false
    property int  repeatMode:     0   // 0=off, 1=all, 2=one
    property bool shuffleEnabled: false
    property real volume:         1.0

    signal playPauseClicked()
    signal stopClicked()
    signal nextClicked()
    signal previousClicked()
    signal repeatClicked()
    signal shuffleClicked()
    signal volumeChanged(real vol)

    spacing: Theme.spacing_lg
    anchors.horizontalCenter: parent.horizontalCenter

    // ── Secondary controls (left) ─────────────────────────────────────────
    ToolButton {
        id: shuffleButton
        icon.source: "qrc:/qml/Theme/icon-shuffle.svg"
        icon.color: root.shuffleEnabled
                   ? Theme.colors.primary
                   : Theme.colors.onSurfaceVariant
        onClicked: root.shuffleClicked()
        ToolTip.text: qsTranslate("ArrowPlayer", "Shuffle")
        ToolTip.visible: hovered
    }

    ToolButton {
        id: previousButton
        icon.source: "qrc:/qml/Theme/icon-skip-previous.svg"
        onClicked: root.previousClicked()
        ToolTip.text: qsTranslate("ArrowPlayer", "Previous")
        ToolTip.visible: hovered
    }

    // ── Primary play/pause button (largest) ───────────────────────────
    ToolButton {
        id: playPauseButton
        implicitWidth: 64
        implicitHeight: 64
        icon.source: root.isPlaying
                     ? "qrc:/qml/Theme/icon-pause.svg"
                     : "qrc:/qml/Theme/icon-play.svg"
        icon.width: 40
        icon.height: 40
        icon.color: Theme.colors.onPrimary
        padding: Theme.spacing_sm

        background: Rectangle {
            color: Theme.colors.primary
            radius: Theme.radius_full
        }

        onClicked: root.playPauseClicked()
        ToolTip.text: root.isPlaying
                      ? qsTranslate("ArrowPlayer", "Pause")
                      : qsTranslate("ArrowPlayer", "Play")
        ToolTip.visible: hovered

        scale: playPauseButton.pressed ? 0.92 : 1.0
        Behavior on scale {
            NumberAnimation { duration: Theme.duration("instant") }
        }
    }

    ToolButton {
        id: nextButton
        icon.source: "qrc:/qml/Theme/icon-skip-next.svg"
        onClicked: root.nextClicked()
        ToolTip.text: qsTranslate("ArrowPlayer", "Next")
        ToolTip.visible: hovered
    }

    // ── Repeat ─────────────────────────────────────────────────────────
    ToolButton {
        id: repeatButton
        icon.source: repeatMode === 2
                     ? "qrc:/qml/Theme/icon-repeat-one.svg"
                     : "qrc:/qml/Theme/icon-repeat.svg"
        icon.color: repeatMode > 0
                   ? Theme.colors.primary
                   : Theme.colors.onSurfaceVariant
        onClicked: root.repeatClicked()
        ToolTip.text: repeatMode === 0 ? qsTranslate("ArrowPlayer", "Repeat: Off")
                   : repeatMode === 1 ? qsTranslate("ArrowPlayer", "Repeat: All")
                                      : qsTranslate("ArrowPlayer", "Repeat: One")
        ToolTip.visible: hovered
    }

    // ── Volume ─────────────────────────────────────────────────────────
    Item { width: Theme.spacing_lg }

    ToolButton {
        id: volumeButton
        icon.source: root.volume === 0
                     ? "qrc:/qml/Theme/icon-volume-mute.svg"
                     : root.volume < 0.5
                       ? "qrc:/qml/Theme/icon-volume-low.svg"
                       : "qrc:/qml/Theme/icon-volume-high.svg"
        onClicked: root.volumeChanged(0)  // toggle mute — controller resolves it
        ToolTip.text: qsTranslate("ArrowPlayer", "Mute")
        ToolTip.visible: hovered
    }

    Slider {
        id: volumeSlider
        from: 0
        to: 1
        value: root.volume
        implicitWidth: 120
        opacity: volumeSlider.hovered || volumeSlider.activeFocus ? 1.0 : 0.7
        Behavior on opacity {
            NumberAnimation { duration: Theme.duration("fast") }
        }
        onMoved: root.volumeChanged(volumeSlider.value)
    }
}
