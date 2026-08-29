// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Settings dialog — spec §7.1 layer 5 (PRESENTATION), §12.4.

#include "arrow/ui/settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QAbstractItemView>

// ---------------------------------------------------------------------------
// Internal tab class declarations — each tab is a QWidget subclass.
// Declared in the .cpp to keep the public header clean.
// ---------------------------------------------------------------------------

class GeneralTab final : public QWidget {
    Q_OBJECT
  public:
    explicit GeneralTab(QWidget* parent = nullptr);
  private:
    QCheckBox* close_to_tray_ = nullptr;
    QCheckBox* start_minimized_ = nullptr;
    QCheckBox* restore_session_ = nullptr;
    QComboBox* language_combo_ = nullptr;
};

class AudioTab final : public QWidget {
    Q_OBJECT
  public:
    explicit AudioTab(QWidget* parent = nullptr);
  private:
    QComboBox* output_device_combo_ = nullptr;
    QCheckBox* exclusive_mode_ = nullptr;
    QSpinBox* buffer_size_spin_ = nullptr;
    QComboBox* resampling_quality_combo_ = nullptr;
};

class LibraryTab final : public QWidget {
    Q_OBJECT
  public:
    explicit LibraryTab(QWidget* parent = nullptr);
  private:
    QListWidget* watch_folders_list_ = nullptr;
    QPushButton* add_folder_btn_ = nullptr;
    QPushButton* remove_folder_btn_ = nullptr;
    QCheckBox* watch_for_changes_ = nullptr;
    QCheckBox* automatic_rescan_ = nullptr;
    QSpinBox* rescan_interval_spin_ = nullptr;
};

class PlaybackTab final : public QWidget {
    Q_OBJECT
  public:
    explicit PlaybackTab(QWidget* parent = nullptr);
  private:
    QCheckBox* gapless_playback_ = nullptr;
    QSpinBox* crossfade_duration_spin_ = nullptr;
    QCheckBox* fade_on_pause_ = nullptr;
    QSpinBox* fade_duration_spin_ = nullptr;
    QCheckBox* replay_gain_enabled_ = nullptr;
    QComboBox* replay_gain_mode_combo_ = nullptr;
    QSpinBox* replay_gain_preamp_spin_ = nullptr;
};

class AppearanceTab final : public QWidget {
    Q_OBJECT
  public:
    explicit AppearanceTab(QWidget* parent = nullptr);
  private:
    QComboBox* theme_combo_ = nullptr;
    QComboBox* icon_theme_combo_ = nullptr;
    QComboBox* font_scale_combo_ = nullptr;
    QCheckBox* smooth_scrolling_ = nullptr;
    QCheckBox* use_native_window_borders_ = nullptr;
};

class ShortcutsTab final : public QWidget {
    Q_OBJECT
  public:
    explicit ShortcutsTab(QWidget* parent = nullptr);
  private:
    QTableWidget* shortcuts_table_ = nullptr;
    QPushButton* reset_shortcuts_btn_ = nullptr;
    QPushButton* import_shortcuts_btn_ = nullptr;
    QPushButton* export_shortcuts_btn_ = nullptr;
};

class AboutTab final : public QWidget {
    Q_OBJECT
  public:
    explicit AboutTab(const QString& version, const QString& git_sha,
                     bool git_dirty, QWidget* parent = nullptr);
  private:
    void addLink(QVBoxLayout* layout, const QString& url, const QString& label);
};

// ===========================================================================
// SettingsDialog
// ===========================================================================

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumSize(680, 500);
    resize(760, 560);
    setModal(false);

    auto* main_layout = new QVBoxLayout(this);

    tab_widget_ = new QTabWidget(this);
    main_layout->addWidget(tab_widget_);

    tab_widget_->addTab(new GeneralTab(this), tr("General"));
    tab_widget_->addTab(new AudioTab(this), tr("Audio"));
    tab_widget_->addTab(new LibraryTab(this), tr("Library"));
    tab_widget_->addTab(new PlaybackTab(this), tr("Playback"));
    tab_widget_->addTab(new AppearanceTab(this), tr("Appearance"));
    tab_widget_->addTab(new ShortcutsTab(this), tr("Shortcuts"));
    tab_widget_->addTab(new AboutTab(
        QString::fromUtf8("TODO"), QString::fromUtf8("TODO"), false, this),
        tr("About"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(buttons);
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::refreshAndShow() {
    // TODO: reload all tab values from DataStore
    show();
    raise();
    activateWindow();
}

// ===========================================================================
// GeneralTab
// ===========================================================================

GeneralTab::GeneralTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* behaviour_group = new QGroupBox(tr("Behaviour"), this);
    auto* behaviour_layout = new QVBoxLayout(behaviour_group);

    close_to_tray_ = new QCheckBox(tr("Close to system tray"), this);
    behaviour_layout->addWidget(close_to_tray_);

    start_minimized_ = new QCheckBox(tr("Start minimized"), this);
    behaviour_layout->addWidget(start_minimized_);

    restore_session_ = new QCheckBox(tr("Restore previous session on startup"), this);
    restore_session_->setChecked(true);
    behaviour_layout->addWidget(restore_session_);

    layout->addWidget(behaviour_group);

    auto* language_group = new QGroupBox(tr("Language"), this);
    auto* language_layout = new QHBoxLayout(language_group);

    language_layout->addWidget(new QLabel(tr("Language:"), this));
    language_combo_ = new QComboBox(this);
    language_combo_->addItems({tr("System default"), u"English"_s, u"Indonesia"_s});
    language_layout->addWidget(language_combo_);
    language_layout->addStretch();

    layout->addWidget(language_group);
    layout->addStretch();
}

// ===========================================================================
// AudioTab
// ===========================================================================

AudioTab::AudioTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* output_group = new QGroupBox(tr("Output"), this);
    auto* output_layout = new QFormLayout(output_group);

    output_layout->addRow(tr("Output device:"), output_device_combo_ = new QComboBox(this));
    output_device_combo_->addItems({tr("System default (automatic)")});

    exclusive_mode_ = new QCheckBox(tr("Exclusive mode (bit-perfect)"), this);
    output_layout->addRow(u""_s, exclusive_mode_);

    layout->addWidget(output_group);

    auto* buffer_group = new QGroupBox(tr("Performance"), this);
    auto* buffer_layout = new QFormLayout(buffer_group);

    buffer_size_spin_ = new QSpinBox(this);
    buffer_size_spin_->setRange(64, 8192);
    buffer_size_spin_->setSingleStep(64);
    buffer_size_spin_->setSuffix(u" ms"_s);
    buffer_size_spin_->setValue(128);
    buffer_layout->addRow(tr("Buffer size:"), buffer_size_spin_);

    resampling_quality_combo_ = new QComboBox(this);
    resampling_quality_combo_->addItems({
        tr("Low (fastest)"), tr("Medium"), tr("High"), tr("Very high (slowest)")});
    buffer_layout->addRow(tr("Resampling:"), resampling_quality_combo_);

    layout->addWidget(buffer_group);
    layout->addStretch();
}

// ===========================================================================
// LibraryTab
// ===========================================================================

LibraryTab::LibraryTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* folders_group = new QGroupBox(tr("Watch folders"), this);
    auto* folders_layout = new QVBoxLayout(folders_group);

    auto* folders_top_layout = new QHBoxLayout();
    watch_folders_list_ = new QListWidget(this);
    watch_folders_list_->setMaximumHeight(120);
    folders_top_layout->addWidget(watch_folders_list_);

    auto* folders_btn_layout = new QVBoxLayout();
    add_folder_btn_ = new QPushButton(tr("Add…"), this);
    remove_folder_btn_ = new QPushButton(tr("Remove"), this);
    folders_btn_layout->addWidget(add_folder_btn_);
    folders_btn_layout->addWidget(remove_folder_btn_);
    folders_btn_layout->addStretch();
    folders_top_layout->addLayout(folders_btn_layout);

    folders_layout->addLayout(folders_top_layout);

    auto* watch_layout = new QHBoxLayout();
    watch_for_changes_ = new QCheckBox(tr("Watch for file changes"), this);
    watch_layout->addWidget(watch_for_changes_);
    watch_layout->addStretch();
    folders_layout->addLayout(watch_layout);

    auto* rescan_layout = new QHBoxLayout();
    automatic_rescan_ = new QCheckBox(tr("Automatic rescan:"), this);
    rescan_interval_spin_ = new QSpinBox(this);
    rescan_interval_spin_->setRange(1, 1440);
    rescan_interval_spin_->setSuffix(tr(" minutes"));
    rescan_interval_spin_->setValue(60);
    rescan_layout->addWidget(automatic_rescan_);
    rescan_layout->addWidget(rescan_interval_spin_);
    rescan_layout->addStretch();
    folders_layout->addLayout(rescan_layout);

    layout->addWidget(folders_group);
    layout->addStretch();
}

// ===========================================================================
// PlaybackTab
// ===========================================================================

PlaybackTab::PlaybackTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* gapless_group = new QGroupBox(tr("Gapless playback"), this);
    auto* gapless_layout = new QVBoxLayout(gapless_group);

    gapless_playback_ = new QCheckBox(tr("Enable gapless playback"), this);
    gapless_playback_->setChecked(true);
    gapless_layout->addWidget(gapless_playback_);

    layout->addWidget(gapless_group);

    auto* crossfade_group = new QGroupBox(tr("Crossfade"), this);
    auto* crossfade_layout = new QFormLayout(crossfade_group);

    crossfade_duration_spin_ = new QSpinBox(this);
    crossfade_duration_spin_->setRange(0, 30);
    crossfade_duration_spin_->setSuffix(u" s"_s);
    crossfade_duration_spin_->setValue(0);
    crossfade_layout->addRow(tr("Duration:"), crossfade_duration_spin_);

    layout->addWidget(crossfade_group);

    auto* fade_group = new QGroupBox(tr("Fade on pause / stop"), this);
    auto* fade_layout = new QVBoxLayout(fade_group);

    fade_on_pause_ = new QCheckBox(tr("Enable fade on pause/stop"), this);
    fade_layout->addWidget(fade_on_pause_);

    auto* fade_row_layout = new QHBoxLayout();
    fade_row_layout->addWidget(new QLabel(tr("Fade duration:"), this));
    fade_duration_spin_ = new QSpinBox(this);
    fade_duration_spin_->setRange(50, 2000);
    fade_duration_spin_->setSingleStep(50);
    fade_duration_spin_->setSuffix(u" ms"_s);
    fade_duration_spin_->setValue(300);
    fade_row_layout->addWidget(fade_duration_spin_);
    fade_row_layout->addStretch();
    fade_layout->addLayout(fade_row_layout);

    layout->addWidget(fade_group);

    auto* rg_group = new QGroupBox(tr("ReplayGain"), this);
    auto* rg_layout = new QFormLayout(rg_group);

    replay_gain_enabled_ = new QCheckBox(tr("Enable ReplayGain"), this);
    rg_layout->addRow(u""_s, replay_gain_enabled_);

    replay_gain_mode_combo_ = new QComboBox(this);
    replay_gain_mode_combo_->addItems({
        tr("Album mode"), tr("Track mode"), tr("Adaptive (prefer track)")});
    rg_layout->addRow(tr("Mode:"), replay_gain_mode_combo_);

    replay_gain_preamp_spin_ = new QSpinBox(this);
    replay_gain_preamp_spin_->setRange(-15, 15);
    replay_gain_preamp_spin_->setSuffix(u" dB"_s);
    replay_gain_preamp_spin_->setValue(0);
    rg_layout->addRow(tr("Pre-amp:"), replay_gain_preamp_spin_);

    layout->addWidget(rg_group);
    layout->addStretch();
}

// ===========================================================================
// AppearanceTab
// ===========================================================================

AppearanceTab::AppearanceTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    auto* theme_group = new QGroupBox(tr("Theme"), this);
    auto* theme_layout = new QFormLayout(theme_group);

    theme_combo_ = new QComboBox(this);
    theme_combo_->addItems({tr("System default"), tr("Light"), tr("Dark"), tr("OLED Dark")});
    theme_layout->addRow(tr("Color scheme:"), theme_combo_);

    icon_theme_combo_ = new QComboBox(this);
    icon_theme_combo_->addItems({tr("Follow system"), tr("Light icons"), tr("Dark icons")});
    theme_layout->addRow(tr("Icons:"), icon_theme_combo_);

    font_scale_combo_ = new QComboBox(this);
    font_scale_combo_->addItems({u"85%"_s, u"100%"_s, u"115%"_s, u"130%"_s});
    theme_layout->addRow(tr("Font scale:"), font_scale_combo_);

    layout->addWidget(theme_group);

    auto* misc_group = new QGroupBox(tr("Interface"), this);
    auto* misc_layout = new QVBoxLayout(misc_group);

    smooth_scrolling_ = new QCheckBox(tr("Smooth scrolling"), this);
    smooth_scrolling_->setChecked(true);
    misc_layout->addWidget(smooth_scrolling_);

    use_native_window_borders_ = new QCheckBox(tr("Use native window borders (Linux)"), this);
    use_native_window_borders_->setChecked(true);
    misc_layout->addWidget(use_native_window_borders_);

    layout->addWidget(misc_group);
    layout->addStretch();
}

// ===========================================================================
// ShortcutsTab
// ===========================================================================

ShortcutsTab::ShortcutsTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    shortcuts_table_ = new QTableWidget(0, 2, this);
    shortcuts_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    shortcuts_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    shortcuts_table_->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut")});
    shortcuts_table_->horizontalHeader()->setStretchLastSection(true);
    shortcuts_table_->verticalHeader()->setVisible(false);

    const QStringList default_actions = {
        tr("Play / Pause"),
        tr("Stop"),
        tr("Next track"),
        tr("Previous track"),
        tr("Volume up"),
        tr("Volume down"),
        tr("Toggle mute"),
        tr("Seek forward 5 s"),
        tr("Seek backward 5 s"),
        tr("Toggle now-playing view"),
        tr("Toggle library"),
        tr("Toggle queue"),
        tr("Toggle equalizer"),
    };
    shortcuts_table_->setRowCount(static_cast<int>(default_actions.size()));
    for (int r = 0; r < default_actions.size(); ++r) {
        shortcuts_table_->setItem(r, 0, new QTableWidgetItem(default_actions[r]));
        shortcuts_table_->setItem(r, 1, new QTableWidgetItem(u"---"_s));
    }

    layout->addWidget(shortcuts_table_, 1);

    auto* btn_layout = new QHBoxLayout();
    reset_shortcuts_btn_ = new QPushButton(tr("Reset to Defaults"), this);
    import_shortcuts_btn_ = new QPushButton(tr("Import…"), this);
    export_shortcuts_btn_ = new QPushButton(tr("Export…"), this);
    btn_layout->addWidget(reset_shortcuts_btn_);
    btn_layout->addWidget(import_shortcuts_btn_);
    btn_layout->addWidget(export_shortcuts_btn_);
    btn_layout->addStretch();

    layout->addLayout(btn_layout);
}

// ===========================================================================
// AboutTab
// ===========================================================================

AboutTab::AboutTab(const QString& version, const QString& git_sha,
                   bool git_dirty, QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignHCenter);

    layout->addSpacing(24);

    auto* logo = new QLabel(this);
    logo->setFixedSize(80, 80);
    logo->setStyleSheet(u"background:#5c3d99;border-radius:16px;"_s);
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    layout->addSpacing(12);

    auto* name_label = new QLabel(u"Arrow Player"_s, this);
    QFont name_font = name_label->font();
    name_font.setBold(true);
    name_font.setPointSizeF(name_font.pointSizeF() * 1.5);
    name_label->setFont(name_font);
    name_label->setAlignment(Qt::AlignHCenter);
    layout->addWidget(name_label);

    layout->addWidget(new QLabel(tr("Version %1").arg(version), this));

    if (git_dirty) {
        auto* dirty_label = new QLabel(
            tr("Built from a working tree with uncommitted changes"), this);
        dirty_label->setStyleSheet(u"color:orange;"_s);
        layout->addWidget(dirty_label);
    }

    layout->addWidget(
        new QLabel(tr("Commit: %1").arg(git_sha), this));

    layout->addSpacing(16);

    auto* desc_label = new QLabel(
        tr("Free, open-source, privacy-first music player.\n"
           "Licensed under MPL-2.0."), this);
    desc_label->setAlignment(Qt::AlignHCenter);
    layout->addWidget(desc_label);

    addLink(layout, u"https://arrow-player.org"_s, tr("Website"));
    addLink(layout, u"https://github.com/arrow-player/arrow-player"_s,
            tr("Source code"));

    layout->addStretch();
}

void AboutTab::addLink(QVBoxLayout* layout, const QString& url,
                       const QString& label) {
    auto* link = new QLabel(
        u"<a href=\"%1\">%2</a>"_s.arg(url, label), this);
    link->setOpenExternalLinks(true);
    link->setAlignment(Qt::AlignHCenter);
    layout->addWidget(link);
}

#include "settings_dialog.moc"
