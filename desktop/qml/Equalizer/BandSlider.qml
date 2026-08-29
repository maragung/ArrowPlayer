// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Band slider — spec §7.1 layer 5 (PRESENTATION), REQ-AUD-088.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string bandFrequency: "1k"
    property real bandGain: 0.0
    property int bandIndex: 0
    property bool enabled: true

    signal gainChanged(real gainDb)

    Label {
        text: root.bandFrequency + (root.bandFrequency.length <= 3 ? " Hz" : "")
        font.pixelSize: 12
        color: root.enabled ? Theme.colors.onSurface : Theme.colors.onSurfaceTertiary
        horizontalAlignment: Text.AlignHCenter
        Layout.alignment: Qt.AlignHCenter
    }

    Label {
        text: root.bandGain >= 0
              ? "+" + root.bandGain.toFixed(1)
              : root.bandGain.toFixed(1)
        font.pixelSize: 12
        color: root.bandGain !== 0
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
        value: root.bandGain
        stepSize: 0.5
        enabled: root.enabled
        Layout.preferredHeight: 160
        Layout.preferredWidth: 28

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onDoubleClicked: {
                bandSlider.value = 0
                root.gain_changed(0)
            }
        }

        onMoved: root.gainChanged(bandSlider.value)
    }

    Label {
        text: "+12"
        font.pixelSize: 11
        color: Theme.colors.onSurfaceTertiary
        horizontalAlignment: Text.AlignHCenter
        Layout.alignment: Qt.AlignHCenter
    }

    Label {
        text: "-12"
        font.pixelSize: 11
        color: Theme.colors.onSurfaceTertiary
        horizontalAlignment: Text.AlignHCenter
        Layout.alignment: Qt.AlignHCenter
    }
}
