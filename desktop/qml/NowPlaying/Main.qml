// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Now Playing — spec §7.1 layer 5 (PRESENTATION), §12.1.
// QML skin-driven surface driven by ThemeTokens and PlaybackService signals.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "NowPlayingRoot"

    // -----------------------------------------------------------------------
    // Layout: two-column — album art + track info (left), transport + seekbar
    // (right).  Landscape-first; portrait collapses to single column.
    // -----------------------------------------------------------------------
    RowLayout {
        id: mainRow
        anchors.fill: parent
        spacing: Theme.spacing_xl

        // ── Left column: album art ─────────────────────────────────────────
        ColumnLayout {
            id: leftColumn
            Layout.preferredWidth: parent.width * 0.45
            Layout.fillHeight: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacing_lg

            AlbumArt {
                id: albumArt
                Layout.preferredWidth: parent.width
                Layout.preferredHeight: Layout.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                artwork: currentArtworkUrl
            }

            TrackInfo {
                Layout.fillWidth: true
                artist: currentArtist
                title: currentTitle
                album: currentAlbum
                year: currentYear
                duration: currentDuration
            }
        }

        // ── Right column: seekbar + transport ───────────────────────────────
        ColumnLayout {
            id: rightColumn
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing_lg

            // Seekbar with A-B repeat markers
            SeekBar {
                id: seekBar
                Layout.fillWidth: true
                currentPosition: playbackPositionMs
                duration: playbackDurationMs
                abLoopStart: abLoopStartMs
                abLoopEnd: abLoopEndMs
                onSeekRequested: function(pos) {
                    root.seekRequested(pos)
                }
                onSetLoopA: root.setLoopARequested()
                onSetLoopB: root.setLoopBRequested()
                onClearLoop: root.clearLoopRequested()
            }

            // Time labels: elapsed | remaining
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: formatTime(playbackPositionMs / 1000)
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: playbackDurationMs > 0
                          ? "-" + formatTime((playbackDurationMs - playbackPositionMs) / 1000)
                          : "--:--"
                    font.pixelSize: 12
                    color: Theme.colors.onSurfaceSecondary
                }
            }

            // Transport controls
            TransportControls {
                id: transportControls
                Layout.alignment: Qt.AlignHCenter
                isPlaying: playbackPlaying
                repeatMode: repeatMode
                shuffleEnabled: shuffleEnabled
                volume: playbackVolume

                onPlayPauseClicked: root.playPauseRequested()
                onStopClicked: root.stopRequested()
                onNextClicked: root.nextRequested()
                onPreviousClicked: root.previousRequested()
                onRepeatClicked: root.repeatToggleRequested()
                onShuffleClicked: root.shuffleToggleRequested()
                onVolumeChanged: function(vol) { root.volumeChangeRequested(vol) }
            }

            // Secondary actions row
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacing_md

                ToolButton {
                    id: queueButton
                    text: qsTranslate("ArrowPlayer", "Queue")
                    onClicked: root.showQueueRequested()
                }

                ToolButton {
                    id: eqButton
                    text: qsTranslate("ArrowPlayer", "EQ")
                    onClicked: root.showEqualizerRequested()
                }

                ToolButton {
                    id: abButton
                    text: "A-B"
                    onClicked: root.abLoopCycleRequested()
                    ToolTip.text: qsTranslate("ArrowPlayer", "A–B Loop")
                    ToolTip.visible: hovered
                }

                ToolButton {
                    id: lyricsButton
                    text: qsTranslate("ArrowPlayer", "Lyrics")
                    onClicked: root.showLyricsRequested()
                }
            }

            Item { Layout.fillHeight: true }
        }
    }

    // -----------------------------------------------------------------------
    // Private properties — bound to context properties set by the C++ shell
    // (PlaybackController).  These flow in from layer 4 via QQmlContext.
    // -----------------------------------------------------------------------

    property string currentArtist:     ""
    property string currentTitle:      ""
    property string currentAlbum:     ""
    property string currentYear:       ""
    property int    currentDuration:   0      // seconds
    property string currentArtworkUrl:  ""
    property int    playbackPositionMs: 0      // milliseconds
    property int    playbackDurationMs: 0      // milliseconds
    property bool   playbackPlaying:   false
    property real   playbackVolume:    1.0
    property int    repeatMode:       0       // 0=off, 1=all, 2=one
    property bool   shuffleEnabled:   false

    // A-B loop
    property int    abLoopStartMs:    -1
    property int    abLoopEndMs:      -1

    // -----------------------------------------------------------------------
    // Signals — flow back to C++ shell (MainWindow → layer 4 controller)
    // -----------------------------------------------------------------------
    signal playPauseRequested()
    signal stopRequested()
    signal nextRequested()
    signal previousRequested()
    signal repeatToggleRequested()
    signal shuffleToggleRequested()
    signal volumeChangeRequested(real volume)
    signal seekRequested(int positionMs)
    signal showQueueRequested()
    signal showEqualizerRequested()
    signal showLyricsRequested()
    signal abLoopCycleRequested()
    signal setLoopARequested()
    signal setLoopBRequested()
    signal clearLoopRequested()

    // -----------------------------------------------------------------------
    // Helper
    // -----------------------------------------------------------------------
    function formatTime(totalSeconds) {
        if (!totalSeconds || totalSeconds < 0) return "--:--";
        const h = Math.floor(totalSeconds / 3600);
        const m = Math.floor((totalSeconds % 3600) / 60);
        const s = Math.floor(totalSeconds % 60);
        if (h > 0) {
            return "%1:%2:%3".arg(h).arg(m.toString().padStart(2, "0"))
                                 .arg(s.toString().padStart(2, "0"));
        }
        return "%1:%2".arg(m).arg(s.toString().padStart(2, "0"));
    }

    // -----------------------------------------------------------------------
    // Responsive layout: portrait collapses to single column
    // -----------------------------------------------------------------------
    states: State {
        name: "portrait"
        when: root.width < 700
        PropertyChanges {
            target: leftColumn
            Layout.preferredWidth: parent.width
        }
        PropertyChanges {
            target: rightColumn
            Layout.preferredWidth: parent.width
        }
    }
}
