// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Equalizer UI — spec §7.1 layer 5 (PRESENTATION), REQ-AUD-088.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "EqualizerRoot"

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing_lg
        anchors.margins: Theme.spacing_lg

        // ── Header ─────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTranslate("ArrowPlayer", "Equalizer")
                font.pixelSize: 24
                font.weight: Font.DemiBold
                Layout.alignment: Qt.AlignVCenter
            }

            Switch {
                id: eqEnabledSwitch
                checked: eqEnabled
                onCheckedChanged: root.setEnabledRequested(checked)
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: Theme.spacing_lg
            }

            Item { Layout.fillWidth: true }

            // 10-band / 18-band toggle
            RowLayout {
                spacing: 2

                Rectangle {
                    width: 32
                    height: 28
                    radius: Theme.radius_sm
                    color: bandCount === 10
                           ? Theme.colors.primary
                           : Theme.colors.surfaceContainerHighest
                    Label {
                        anchors.centerIn: parent
                        text: "10"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: bandCount === 10
                               ? Theme.colors.onPrimary
                               : Theme.colors.onSurfaceVariant
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.setBandCountRequested(10)
                    }
                }

                Rectangle {
                    width: 32
                    height: 28
                    radius: Theme.radius_sm
                    color: bandCount === 18
                           ? Theme.colors.primary
                           : Theme.colors.surfaceContainerHighest
                    Label {
                        anchors.centerIn: parent
                        text: "18"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: bandCount === 18
                               ? Theme.colors.onPrimary
                               : Theme.colors.onSurfaceVariant
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.setBandCountRequested(18)
                    }
                }
            }
        }

        // ── Frequency response graph ────────────────────────────────────────
        FrequencyGraph {
            id: freqGraph
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            enabled: eqEnabled
            preampGain: preampGain
            bandGains: bandGains
        }

        // ── Pre-amp ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing_md

            Label {
                text: qsTranslate("ArrowPlayer", "Pre-amp:")
                font.pixelSize: 14
                Layout.preferredWidth: 70
            }

            Slider {
                id: preampSlider
                from: -12
                to: 12
                value: preampGain
                stepSize: 0.5
                Layout.fillWidth: true
                enabled: eqEnabled

                // dB label
                Label {
                    anchors.horizontalCenter: parent.handle.horizontalCenter
                    anchors.bottom: parent.top
                    anchors.bottomMargin: 4
                    text: preampSlider.value.toFixed(1) + " dB"
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                }
            }

            ToolButton {
                text: "↺"
                onClicked: {
                    preampSlider.value = 0
                    root.setPreampRequested(0)
                }
                ToolTip.text: qsTranslate("ArrowPlayer", "Reset pre-amp")
                ToolTip.visible: hovered
            }
        }

        // ── Band sliders ───────────────────────────────────────────────────
        // 10-band frequencies (Hz): 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k
        // 18-band adds: 16, 24, 40, 50k, 63k
        RowLayout {
            id: bandsRow
            Layout.fillWidth: true
            spacing: Theme.spacing_xs

            Repeater {
                id: bandsRepeater
                model: [
                    { freq: "16",  bandIndex: 0, minBands: 18 },
                    { freq: "24",  bandIndex: 1, minBands: 18 },
                    { freq: "31",  bandIndex: 2, minBands: 10 },
                    { freq: "40",  bandIndex: 3, minBands: 18 },
                    { freq: "62",  bandIndex: 4, minBands: 10 },
                    { freq: "125", bandIndex: 5, minBands: 10 },
                    { freq: "250", bandIndex: 6, minBands: 10 },
                    { freq: "500", bandIndex: 7, minBands: 10 },
                    { freq: "1k",  bandIndex: 8, minBands: 10 },
                    { freq: "2k",  bandIndex: 9, minBands: 10 },
                    { freq: "4k",  bandIndex: 10, minBands: 10 },
                    { freq: "8k",  bandIndex: 11, minBands: 10 },
                    { freq: "16k", bandIndex: 12, minBands: 10 },
                    { freq: "50k", bandIndex: 13, minBands: 18 },
                    { freq: "63k", bandIndex: 14, minBands: 18 },
                ]

                delegate: ColumnLayout {
                    visible: bandCount >= modelData.minBands
                    spacing: 2

                    Label {
                        text: modelData.freq + (modelData.freq.length <= 3 ? " Hz" : "")
                        font.pixelSize: 10
                        color: Theme.colors.onSurfaceSecondary
                        horizontalAlignment: Text.AlignHCenter
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Label {
                        id: gainLabel
                        text: {
                            const g = bandGains[modelData.bandIndex] || 0
                            return g >= 0 ? "+" + g.toFixed(1) : g.toFixed(1)
                        }
                        font.pixelSize: 10
                        color: (bandGains[modelData.bandIndex] || 0) !== 0
                               ? Theme.colors.primary
                               : Theme.colors.onSurfaceSecondary
                        horizontalAlignment: Text.AlignHCenter
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Slider {
                        id: bandSlider
                        orientation: Qt.Vertical
                        from: -12
                        to: 12
                        value: bandGains[modelData.bandIndex] || 0
                        stepSize: 0.5
                        enabled: eqEnabled && bandCount >= modelData.minBands
                        Layout.preferredHeight: 160
                        Layout.preferredWidth: 28

                        // Double-click to reset
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onDoubleClicked: {
                                bandSlider.value = 0
                                root.setBandGainRequested(modelData.bandIndex, 0)
                            }
                        }

                        onMoved: root.setBandGainRequested(modelData.bandIndex, bandSlider.value)
                    }

                    Label { text: "+12"; font.pixelSize: 10; color: Theme.colors.onSurfaceTertiary; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "-12"; font.pixelSize: 10; color: Theme.colors.onSurfaceTertiary; horizontalAlignment: Text.AlignHCenter }
                }
            }
        }

        // ── Presets ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTranslate("ArrowPlayer", "Preset:")
                font.pixelSize: 14
            }

            ComboBox {
                id: presetCombo
                Layout.preferredWidth: 200
                model: presetNames
                currentIndex: presetIndex
                onCurrentIndexChanged: root.selectPresetRequested(presetCombo.currentIndex)
            }

            Button {
                text: qsTranslate("ArrowPlayer", "Save Preset…")
                onClicked: root.savePresetRequested()
            }

            Button {
                text: qsTranslate("ArrowPlayer", "Reset All")
                onClicked: root.resetAllRequested()
            }

            Item { Layout.fillWidth: true }
        }
    }

    // ── Properties ──────────────────────────────────────────────────────────
    property bool eqEnabled: false
    property int  bandCount: 10
    property real preampGain: 0.0
    property int  presetIndex: 0
    property var  bandGains: []
    property list<string> presetNames: [
        qsTranslate("ArrowPlayer", "Flat"),
        qsTranslate("ArrowPlayer", "Rock"),
        qsTranslate("ArrowPlayer", "Pop"),
        qsTranslate("ArrowPlayer", "Jazz"),
        qsTranslate("ArrowPlayer", "Classical"),
        qsTranslate("ArrowPlayer", "Bass Boost"),
        qsTranslate("ArrowPlayer", "Treble Boost"),
        qsTranslate("ArrowPlayer", "Vocal"),
        qsTranslate("ArrowPlayer", "Custom")
    ]

    // ── Signals ────────────────────────────────────────────────────────────
    signal setEnabledRequested(bool enabled)
    signal setBandCountRequested(int count)
    signal setBandGainRequested(int bandIndex, real gainDb)
    signal setPreampRequested(real gainDb)
    signal selectPresetRequested(int presetIndex)
    signal savePresetRequested()
    signal resetAllRequested()
}
