// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Main window — spec §7.1 layer 5 (PRESENTATION), §12.3–§12.4.
//
// Qt Widgets shell owning: window chrome (native title bar on Linux, §12.3),
// menu bar (File / Playback / View / Library / Help), the playlist tab strip
// (REQ-PLS-020), the status bar (track info, bitrate, sample rate, output
// device), and a central container that hosts the embedded QQuickWidget for
// QML-skinned surfaces (Now Playing, Library, Queue, Equalizer).
//
// Window chrome is deliberately native on Linux (REQ-UIX-010); custom chrome
// is v1.x per the task description.  Menu bar and status bar are native Qt
// Widgets; all skin-driven surfaces live in the QML layer.

#pragma once

#include <QPointer>
#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QMenuBar;
class QQuickWidget;
class QStackedWidget;
class QStatusBar;
class QQmlEngine;
class QTimer;

namespace arrow::ui {

class PlaylistTabs;
class SettingsDialog;
struct ShellInfo;

/// The top-level main window.
///
/// Owns the native Qt Widgets shell: menu bar, status bar, playlist tab strip,
/// and a QQuickWidget stack for the QML presentation surfaces.  Zero business
/// logic; all state flows in through the public slots from layer 4 controllers.
class MainWindow final : public QWidget {
    Q_OBJECT

  public:
    /// Constructs the main window.
    ///
    /// @param info     Build identity from the composition root.
    /// @param parent   Parent widget (typically nullptr for a top-level window).
    explicit MainWindow(const ShellInfo& info, QWidget* parent = nullptr);
    ~MainWindow() override;

    // non-copyable, non-movable
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;

  public slots:
    // -----------------------------------------------------------------------
    // Transport state — driven by PlaybackController (layer 4)
    // -----------------------------------------------------------------------
    /// Updates the track displayed in the status bar.
    void onTrackChanged(const QString& artist, const QString& title,
                        const QString& album);
    /// Updates the playback state shown in the transport menu.
    void onPlaybackStateChanged(bool playing);
    /// Updates the volume shown in the status bar.
    void onVolumeChanged(float volume);
    /// Updates the bitrate shown in the status bar.
    void onBitrateChanged(const QString& bitrate);
    /// Updates the sample rate shown in the status bar.
    void onSampleRateChanged(const QString& sample_rate);
    /// Updates the output device name shown in the status bar.
    void onOutputDeviceChanged(const QString& device_name);

    // -----------------------------------------------------------------------
    // Navigation
    // -----------------------------------------------------------------------
    /// Switches the central view to the named QML surface.
    void showView(const QString& view_name);
    /// Opens the settings dialog (non-modal).
    void openSettings();

  signals:
    /// Emitted when the user requests playback of the currently selected item.
    void playRequested();
    /// Emitted when the user requests pause.
    void pauseRequested();
    /// Emitted when the user requests stop.
    void stopRequested();
    /// Emitted when the user requests next track.
    void nextRequested();
    /// Emitted when the user requests previous track.
    void previousRequested();
    /// Emitted when the user requests a seek to \a position_ms.
    void seekRequested(qint64 position_ms);
    /// Emitted when the user adjusts the volume to \a volume (0.0–1.0).
    void volumeChangeRequested(float volume);
    /// Emitted when the user toggles mute.
    void muteToggleRequested();
    /// Emitted when the user toggles repeat mode.
    void repeatToggleRequested();
    /// Emitted when the user toggles shuffle.
    void shuffleToggleRequested();

  private slots:
    void onAddFiles();
    void onAddFolder();
    void onQuit();
    void onAbout();

  private:
    void buildMenuBar();
    void buildStatusBar();
    void buildCentralWidget();
    QQuickWidget* installQmlSurface(const QString& qml_file, const QString& object_name);

    ShellInfo info_;

    // Native chrome
    QMenuBar* menu_bar_ = nullptr;
    QStatusBar* status_bar_ = nullptr;

    // Transport menu actions (updated on state change)
    QAction* play_pause_action_ = nullptr;
    QAction* stop_action_ = nullptr;
    QAction* next_action_ = nullptr;
    QAction* previous_action_ = nullptr;
    QAction* repeat_action_ = nullptr;
    QAction* shuffle_action_ = nullptr;

    // Status bar labels
    QLabel* track_label_ = nullptr;
    QLabel* bitrate_label_ = nullptr;
    QLabel* sample_rate_label_ = nullptr;
    QLabel* output_device_label_ = nullptr;

    // QML surface stack
    QStackedWidget* surface_stack_ = nullptr;
    QPointer<QQuickWidget> now_playing_view_;
    QPointer<QQuickWidget> library_view_;
    QPointer<QQuickWidget> queue_view_;
    QPointer<QQuickWidget> equalizer_view_;

    QPointer<SettingsDialog> settings_dialog_;
};

}  // namespace arrow::ui
