// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Main window — spec §7.1 layer 5 (PRESENTATION), §12.3–§12.4.

#include "arrow/ui/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QQuickWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>

#include "arrow/ui/playlist_tabs.hpp"
#include "arrow/ui/settings_dialog.hpp"

#if QT_CONFIG(qmltabmodel)
#  include <QtQml/QQmlApplicationEngine>
#endif

namespace {

/// Converts a seconds value to a human-readable mm:ss or h:mm:ss string.
QString formatDuration(qint64 seconds) {
    if (seconds < 0) return QStringLiteral("--:--");
    const int h = static_cast<int>(seconds) / 3600;
    const int m = (static_cast<int>(seconds) % 3600) / 60;
    const int s = static_cast<int>(seconds) % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

}  // namespace

namespace arrow::ui {

MainWindow::MainWindow(const ShellInfo& info, QWidget* parent)
    : QWidget(parent), info_(info) {
    setWindowTitle(tr("Arrow Player"));

    // Respect native window decorations on Linux (REQ-UIX-010).
    // On Windows custom chrome is v1.x per the task description; this
    // uses the native frame for now.
    // Use Qt::Window flag for proper OS-level integration.
    setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

    // Minimum size so the Now Playing album art is legible.
    setMinimumSize(900, 640);

    // Layout: menu bar + status bar are managed by QWidget's layout system
    // when we call setMenuBar() / setStatusBar().

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    buildMenuBar();
    buildStatusBar();
    buildCentralWidget();

    // Show the library view by default.
    showView(u"Library"_s);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Menu bar construction
// ---------------------------------------------------------------------------

void MainWindow::buildMenuBar() {
    menu_bar_ = new QMenuBar(this);
    setMenuBar(menu_bar_);

    // ── File ──────────────────────────────────────────────────────────────
    QMenu* file_menu = menu_bar_->addMenu(tr("&File"));
    file_menu->addAction(tr("&Add Files…"), this, &MainWindow::onAddFiles,
                         QKeySequence::Open);
    file_menu->addAction(tr("Add &Folder…"), this, &MainWindow::onAddFolder);
    file_menu->addSeparator();
    file_menu->addAction(tr("&Settings…"), this, &MainWindow::openSettings,
                         QKeySequence::Preferences);
    file_menu->addSeparator();
    file_menu->addAction(tr("&Quit"), this, &MainWindow::onQuit,
                         QKeySequence::Quit);

    // ── Playback ─────────────────────────────────────────────────────────
    QMenu* playback_menu = menu_bar_->addMenu(tr("&Playback"));

    play_pause_action_ = playback_menu->addAction(
        tr("&Play / Pause"), this, [this] { emit playRequested(); },
        Qt::Key_MediaTogglePlayPause);
    play_pause_action_->setCheckable(false);

    stop_action_ = playback_menu->addAction(
        tr("&Stop"), this, [this] { emit stopRequested(); },
        Qt::Key_MediaStop);
    stop_action_->setCheckable(false);

    playback_menu->addSeparator();

    previous_action_ = playback_menu->addAction(
        tr("&Previous"), this, [this] { emit previousRequested(); },
        Qt::Key_MediaPrevious);
    previous_action_->setCheckable(false);

    next_action_ = playback_menu->addAction(
        tr("&Next"), this, [this] { emit nextRequested(); },
        Qt::Key_MediaNext);
    next_action_->setCheckable(false);

    playback_menu->addSeparator();

    repeat_action_ = playback_menu->addAction(
        tr("Re&peat"), this, [this] { emit repeatToggleRequested(); });
    repeat_action_->setCheckable(true);

    shuffle_action_ = playback_menu->addAction(
        tr("&Shuffle"), this, [this] { emit shuffleToggleRequested(); });
    shuffle_action_->setCheckable(true);

    playback_menu->addSeparator();

    // Volume actions
    playback_menu->addAction(tr("Volume &Up"), this,
                             [this] { emit volumeChangeRequested(-1); },
                             Qt::Key_VolumeUp);
    playback_menu->addAction(tr("Volume &Down"), this,
                             [this] { emit volumeChangeRequested(+1); },
                             Qt::Key_VolumeDown);
    playback_menu->addAction(tr("&Mute"), this,
                              [this] { emit muteToggleRequested(); },
                              Qt::Key_VolumeMute);

    // ── View ─────────────────────────────────────────────────────────────
    QMenu* view_menu = menu_bar_->addMenu(tr("&View"));

    view_menu->addAction(tr("&Now Playing"), this,
                         [this] { showView(u"NowPlaying"_s); },
                         Qt::CTRL | Qt::Key_1);
    view_menu->addAction(tr("&Library"), this,
                         [this] { showView(u"Library"_s); },
                         Qt::CTRL | Qt::Key_2);
    view_menu->addAction(tr("&Queue"), this,
                         [this] { showView(u"Queue"_s); },
                         Qt::CTRL | Qt::Key_3);
    view_menu->addAction(tr("&Equalizer"), this,
                         [this] { showView(u"Equalizer"_s); },
                         Qt::CTRL | Qt::Key_4);

    // ── Library ─────────────────────────────────────────────────────────
    QMenu* library_menu = menu_bar_->addMenu(tr("&Library"));
    library_menu->addAction(tr("&Rescan Library"), this, [] {},
                             Qt::CTRL | Qt::Key_R);
    library_menu->addAction(tr("&Search…"), this, [] {},
                             Qt::CTRL | Qt::Key_F);

    // ── Help ─────────────────────────────────────────────────────────────
    QMenu* help_menu = menu_bar_->addMenu(tr("&Help"));
    help_menu->addAction(tr("&Keyboard Shortcuts"), this, [] {},
                          Qt::Key_F1);
    help_menu->addSeparator();
    help_menu->addAction(tr("&About Arrow Player"), this, &MainWindow::onAbout,
                          QKeySequence::HelpContents);
}

void MainWindow::buildStatusBar() {
    status_bar_ = new QStatusBar(this);
    setStatusBar(status_bar_);

    // Left: track info
    track_label_ = new QLabel(tr("No track"), this);
    track_label_->setMinimumWidth(200);
    status_bar_->addWidget(track_label_, 1);

    // Right cluster: bitrate, sample rate, output device
    bitrate_label_ = new QLabel(this);
    bitrate_label_->setMinimumWidth(80);
    status_bar_->addPermanentWidget(bitrate_label_);

    sample_rate_label_ = new QLabel(this);
    sample_rate_label_->setMinimumWidth(90);
    status_bar_->addPermanentWidget(sample_rate_label_);

    output_device_label_ = new QLabel(this);
    output_device_label_->setMinimumWidth(120);
    status_bar_->addPermanentWidget(output_device_label_);
}

void MainWindow::buildCentralWidget() {
    surface_stack_ = new QStackedWidget(this);

    // Install QML surfaces.  Each QQuickWidget embeds a QML file.
    // QML files live in desktop/qml/ and are shipped as Qt Resources.
    now_playing_view_ = installQmlSurface(
        u"qrc:/qml/NowPlaying/Main.qml"_s, u"NowPlayingRoot"_s);
    library_view_ = installQmlSurface(
        u"qrc:/qml/Library/Main.qml"_s, u"LibraryRoot"_s);
    queue_view_ = installQmlSurface(
        u"qrc:/qml/Queue/Main.qml"_s, u"QueueRoot"_s);
    equalizer_view_ = installQmlSurface(
        u"qrc:/qml/Equalizer/Main.qml"_s, u"EqualizerRoot"_s);

    // If a QML file failed to load, the widget is null; addTab with a null
    // widget would assert in debug builds, so guard.
    if (now_playing_view_) surface_stack_->addWidget(now_playing_view_);
    if (library_view_) surface_stack_->addWidget(library_view_);
    if (queue_view_) surface_stack_->addWidget(queue_view_);
    if (equalizer_view_) surface_stack_->addWidget(equalizer_view_);

    layout()->addWidget(surface_stack_);
}

QQuickWidget* MainWindow::installQmlSurface(const QString& qml_file,
                                            const QString& object_name) {
    auto* widget = new QQuickWidget(this);
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    widget->setClearColor(palette().window().color());

    QQmlEngine* engine = widget->engine();
    QObject::connect(
        engine, &QQmlEngine::warnings,
        this, [](const QList<QQmlError>& warnings) {
            for (const QQmlError& e : warnings) {
                qWarning().noquote() << "QML warning:" << e.toString();
            }
        });

    widget->setSource(QUrl(qml_file));
    if (widget->rootObject()) {
        widget->rootObject()->setObjectName(object_name);
    }
    return widget;
}

// ---------------------------------------------------------------------------
// Public slots — driven by PlaybackController / LibraryController (layer 4)
// ---------------------------------------------------------------------------

void MainWindow::onTrackChanged(const QString& artist, const QString& title,
                               const QString& album) {
    QString label;
    if (artist.isEmpty() && title.isEmpty()) {
        label = tr("No track");
    } else if (artist.isEmpty()) {
        label = title;
    } else {
        label = tr("%1 — %2").arg(artist, title);
    }
    if (!album.isEmpty()) {
        label += tr("  (%1)").arg(album);
    }
    track_label_->setText(label);
}

void MainWindow::onPlaybackStateChanged(bool playing) {
    if (play_pause_action_) {
        play_pause_action_->setText(playing ? tr("&Pause") : tr("&Play / Pause"));
    }
}

void MainWindow::onVolumeChanged(float volume) {
    // Volume label shown in status bar is optional; the QML surface shows the
    // slider.  Keep the status bar label brief.
    const int pct = static_cast<int>(qBound(0.0f, volume, 1.0f) * 100.0f);
    track_label_->setToolTip(tr("Volume: %1%").arg(pct));
}

void MainWindow::onBitrateChanged(const QString& bitrate) {
    if (bitrate.isEmpty()) {
        bitrate_label_->clear();
    } else {
        bitrate_label_->setText(bitrate);
    }
}

void MainWindow::onSampleRateChanged(const QString& sample_rate) {
    if (sample_rate.isEmpty()) {
        sample_rate_label_->clear();
    } else {
        sample_rate_label_->setText(sample_rate);
    }
}

void MainWindow::onOutputDeviceChanged(const QString& device_name) {
    if (device_name.isEmpty()) {
        output_device_label_->clear();
    } else {
        output_device_label_->setText(device_name);
    }
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void MainWindow::showView(const QString& view_name) {
    if (view_name == u"NowPlaying"_s && now_playing_view_) {
        surface_stack_->setCurrentWidget(now_playing_view_);
    } else if (view_name == u"Library"_s && library_view_) {
        surface_stack_->setCurrentWidget(library_view_);
    } else if (view_name == u"Queue"_s && queue_view_) {
        surface_stack_->setCurrentWidget(queue_view_);
    } else if (view_name == u"Equalizer"_s && equalizer_view_) {
        surface_stack_->setCurrentWidget(equalizer_view_);
    }
}

void MainWindow::openSettings() {
    if (!settings_dialog_) {
        settings_dialog_ = new SettingsDialog(this);
    }
    settings_dialog_->refreshAndShow();
}

// ---------------------------------------------------------------------------
// File menu slots
// ---------------------------------------------------------------------------

void MainWindow::onAddFiles() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Files"), QString(),
        tr("Audio files (*.mp3 *.flac *.ogg *.m4a *.wav *.aac *.opus);;"
           "All files (*)"));
    if (!files.isEmpty()) {
        // Emit a signal layer 4 listens to; here we only emit, not enqueue.
        // The layer 4 controller decides whether to add to queue or to library.
        for (const QString& f : files) {
            Q_UNUSED(f);
            // Signal pending: emit addFilesRequested(files);
        }
    }
}

void MainWindow::onAddFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Add Folder"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        // Signal pending: emit addFolderRequested(dir);
        Q_UNUSED(dir);
    }
}

void MainWindow::onQuit() {
    QApplication::quit();
}

void MainWindow::onAbout() {
    const QString version = QString::fromUtf8(info_.version.data(),
                                            static_cast<qsizetype>(info_.version.size()));
    const QString sha = QString::fromUtf8(info_.git_sha.data(),
                                          static_cast<qsizetype>(info_.git_sha.size()));
    QString text = tr("Arrow Player %1\nCommit: %2\n\n"
                      "Free, open-source, privacy-first music player.\n"
                      "Licensed under MPL-2.0.")
                      .arg(version, sha);
    if (info_.git_dirty) {
        text += u"\n\n"_s + tr("Built from a working tree with uncommitted changes");
    }

    QMessageBox::about(this, tr("About Arrow Player"), text);
}

}  // namespace arrow::ui
