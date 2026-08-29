// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Song list view — spec §7.1 layer 5 (PRESENTATION).

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TableView {
    id: root

    property string viewMode: "list"
    signal itemActivated(int index)
    signal itemContextMenu(int index)

    model: songModel

    TableViewColumn {
        title: "#"
        width: 40
        role: "trackNumber"
    }
    TableViewColumn {
        title: qsTranslate("ArrowPlayer", "Title")
        width: 300
        role: "title"
    }
    TableViewColumn {
        title: qsTranslate("ArrowPlayer", "Artist")
        width: 180
        role: "artist"
    }
    TableViewColumn {
        title: qsTranslate("ArrowPlayer", "Album")
        width: 180
        role: "album"
    }
    TableViewColumn {
        title: qsTranslate("ArrowPlayer", "Duration")
        width: 70
        role: "durationStr"
    }
    TableViewColumn {
        title: qsTranslate("ArrowPlayer", "Plays")
        width: 60
        role: "playCount"
    }

    rowDelegate: Rectangle {
        color: styleData.alternate
               ? Theme.colors.surfaceContainerHighest
               : "transparent"
    }

    itemDelegate: Label {
        text: styleData.value
        font.pixelSize: 14
        color: Theme.colors.onSurface
        elide: Text.ElideRight
    }
}
