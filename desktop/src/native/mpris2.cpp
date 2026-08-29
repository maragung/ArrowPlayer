// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MPRIS2 D-Bus implementation.
// Uses libdbus-1 directly (no GLib wrapper) for portability.

#include "native/mpris2.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <mutex>
#include <poll.h>
#include <unistd.h>

#if __has_include(<dbus/dbus.h>)
#include <dbus/dbus.h>
#define ARROW_HAVE_DBUS 1
#else
#define ARROW_HAVE_DBUS 0
#endif

#include "core/error.hpp"

namespace {

// MPRIS2 bus name and object paths.
constexpr char BUS_NAME[] = "org.mpris.MediaPlayer2.arrowplayer";
constexpr char OBJECT_PATH[] = "/org/mpris/MediaPlayer2";
constexpr char IFACE_MEDIA_PLAYER2[] = "org.mpris.MediaPlayer2";
constexpr char IFACE_PLAYER[] = "org.mpris.MediaPlayer2.Player";

// DBus match rule for monitoring method calls and signals.
constexpr char MATCH_RULE[] =
    "type='method_call',"
    "interface='org.mpris.MediaPlayer2'";

}  // namespace

namespace arrow::native {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MprisController::MprisController() noexcept = default;

MprisController::~MprisController() noexcept {
    release();
}

[[nodiscard]] bool MprisController::claim() noexcept {
#if ARROW_HAVE_DBUS
    std::unique_lock lock{mutex_};

    if (active_.load(std::memory_order_acquire)) return true;

    DBusError error{};
    dbus_error_init(&error);
    connection_ = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!connection_) {
        dbus_error_free(&error);
        return false;
    }

    const int result = dbus_bus_request_name(
        connection_, BUS_NAME,
        DBUS_NAME_FLAG_DO_NOT_QUEUE,
        &error);
    dbus_error_free(&error);

    if (result < 0) {
        dbus_connection_close(static_cast<DBusConnection*>(connection_));
        connection_ = nullptr;
        return false;
    }
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        dbus_connection_close(static_cast<DBusConnection*>(connection_));
        connection_ = nullptr;
        return false;
    }

    dbus_bus_add_match(static_cast<DBusConnection*>(connection_), MATCH_RULE, nullptr);

    active_.store(true, std::memory_order_release);

    mainloop_running_.store(true, std::memory_order_release);
    mainloop_thread_ = std::thread{[this]() noexcept { mainloop(); }};

    return true;
#else
    return false;
#endif
}

void MprisController::release() noexcept {
    std::unique_lock lock{mutex_};

    active_.store(false, std::memory_order_release);
    mainloop_running_.store(false, std::memory_order_release);

    if (mainloop_thread_.joinable()) {
        mainloop_thread_.join();
    }

#if ARROW_HAVE_DBUS
    if (connection_) {
        dbus_connection_close(static_cast<DBusConnection*>(connection_));
        dbus_connection_unref(static_cast<DBusConnection*>(connection_));
        connection_ = nullptr;
    }
#endif
}

// ---------------------------------------------------------------------------
// State updates (from player)
// ---------------------------------------------------------------------------

void MprisController::update_state(const PlaybackStatus2 status,
                                   const std::chrono::microseconds position,
                                   const double volume,
                                   const LoopStatus loop,
                                   const bool shuffle,
                                   const MprisMetadata& metadata) noexcept {
    std::unique_lock lock{mutex_};

    const bool status_changed = status_ != status;
    const bool position_changed = position_ != position;
    const bool volume_changed = volume_ != volume;
    const bool loop_changed = loop_ != loop;

    status_ = status;
    position_ = position;
    volume_ = volume;
    loop_ = loop;
    metadata_ = metadata;
    last_update_time_ = std::chrono::steady_clock::now();
    position_at_last_update_ = position;

#if ARROW_HAVE_DBUS
    if (!connection_) return;
    auto* conn = static_cast<DBusConnection*>(connection_);
#endif

    // Emit PropertiesChanged for any changed property.
#if ARROW_HAVE_DBUS
    if (status_changed || volume_changed || loop_changed) {
        DBusMessage* msg = dbus_message_new_signal(OBJECT_PATH,
                                                    "org.freedesktop.DBus.Properties",
                                                    "PropertiesChanged");
        if (!msg) return;

        dbus_message_append_args(msg,
                                 DBUS_TYPE_STRING, &IFACE_PLAYER,
                                 DBUS_TYPE_INVALID);

        DBusMessageIter args, dict;
        dbus_message_iter_init_append(msg, &args);
        dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY,
                                         "{sv}", &dict);

        if (status_changed) {
            const char* status_str =
                status == PlaybackStatus2::Playing ? "Playing" :
                status == PlaybackStatus2::Paused ? "Paused" : "Stopped";
            DBusMessageIter entry;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            const char* key = "PlaybackStatus";
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &status_str);
            dbus_message_iter_close_container(&entry, &entry);
            dbus_message_iter_close_container(&dict, &entry);
        }

        if (volume_changed) {
            DBusMessageIter entry;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            const char* key = "Volume";
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "d", &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_DOUBLE, &volume_);
            dbus_message_iter_close_container(&entry, &entry);
            dbus_message_iter_close_container(&dict, &entry);
        }

        if (loop_changed) {
            DBusMessageIter entry;
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
            const char* key = "LoopStatus";
            const char* loop_str =
                loop == LoopStatus::Track ? "Track" :
                loop == LoopStatus::Playlist ? "Playlist" : "None";
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
            dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &entry);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &loop_str);
            dbus_message_iter_close_container(&entry, &entry);
            dbus_message_iter_close_container(&dict, &entry);
        }

        dbus_message_iter_close_container(&args, &dict);
        dbus_connection_send(conn, msg, nullptr);
        dbus_message_unref(msg);
    }

    if (position_changed) {
        DBusMessage* msg = dbus_message_new_signal(OBJECT_PATH,
                                                    IFACE_PLAYER,
                                                    "Seeked");
        if (msg) {
            const std::int64_t pos_us = position.count();
            dbus_message_append_args(msg, DBUS_TYPE_INT64, &pos_us, DBUS_TYPE_INVALID);
            dbus_connection_send(conn, msg, nullptr);
            dbus_message_unref(msg);
        }
    }
#else
    (void)status_changed;
    (void)position_changed;
#endif
}

void MprisController::emit_seeked(const std::chrono::microseconds new_position) noexcept {
#if ARROW_HAVE_DBUS
    std::unique_lock lock{mutex_};
    if (!connection_) return;

    DBusMessage* msg = dbus_message_new_signal(OBJECT_PATH, IFACE_PLAYER, "Seeked");
    if (!msg) return;

    const std::int64_t pos_us = new_position.count();
    dbus_message_append_args(msg, DBUS_TYPE_INT64, &pos_us, DBUS_TYPE_INVALID);
    dbus_connection_send(static_cast<DBusConnection*>(connection_), msg, nullptr);
    dbus_message_unref(msg);
#else
    (void)new_position;
#endif
}

// ---------------------------------------------------------------------------
// D-Bus main loop
// ---------------------------------------------------------------------------

void MprisController::mainloop() noexcept {
#if ARROW_HAVE_DBUS
    while (mainloop_running_.load(std::memory_order_acquire)) {
        auto* conn = static_cast<DBusConnection*>(connection_);
        if (!dbus_connection_read_write_dispatch(conn, 250)) {
            break;
        }
    }

    std::unique_lock lock{mutex_};
    active_.store(false, std::memory_order_release);
    connection_ = nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Property getters
// ---------------------------------------------------------------------------

[[nodiscard]] PlaybackStatus2 MprisController::playback_status() const noexcept {
    std::unique_lock lock{mutex_};
    return status_;
}

[[nodiscard]] LoopStatus MprisController::loop_status() const noexcept {
    std::unique_lock lock{mutex_};
    return loop_;
}

[[nodiscard]] double MprisController::volume() const noexcept {
    std::unique_lock lock{mutex_};
    return volume_;
}

[[nodiscard]] std::chrono::microseconds MprisController::position() const noexcept {
    std::unique_lock lock{mutex_};
    // Interpolate position based on elapsed time since last update.
    if (status_ == PlaybackStatus2::Playing) {
        const auto elapsed = std::chrono::steady_clock::now() - last_update_time_;
        return position_at_last_update_ +
               std::chrono::duration_cast<std::chrono::microseconds>(elapsed);
    }
    return position_;
}

[[nodiscard]] MprisMetadata MprisController::metadata() const noexcept {
    std::unique_lock lock{mutex_};
    return metadata_;
}

[[nodiscard]] bool MprisController::can_go_next() const noexcept {
    return true;
}

[[nodiscard]] bool MprisController::can_go_previous() const noexcept {
    return true;
}

[[nodiscard]] bool MprisController::can_play() const noexcept {
    return true;
}

[[nodiscard]] bool MprisController::can_pause() const noexcept {
    return true;
}

[[nodiscard]] bool MprisController::can_seek() const noexcept {
    return true;
}

[[nodiscard]] bool MprisController::can_control() const noexcept {
    return true;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void MprisController::set_callbacks(const MprisNextCallback on_next,
                                    const MprisPreviousCallback on_previous,
                                    const MprisPauseCallback on_pause,
                                    const MprisPlayCallback on_play,
                                    const MprisStopCallback on_stop,
                                    const MprisSeekCallback on_seek,
                                    const MprisSetPositionCallback on_set_position,
                                    const MprisOpenUriCallback on_open_uri) noexcept {
    std::unique_lock lock{mutex_};
    on_next_ = on_next;
    on_previous_ = on_previous;
    on_pause_ = on_pause;
    on_play_ = on_play;
    on_stop_ = on_stop;
    on_seek_ = on_seek;
    on_set_position_ = on_set_position;
    on_open_uri_ = on_open_uri;
}

// ---------------------------------------------------------------------------
// D-Bus method dispatch
// ---------------------------------------------------------------------------

void MprisController::on_method_call(const char* const interface,
                                     const char* const member,
                                     const std::uint32_t serial) noexcept {
    std::unique_lock lock{mutex_};

    // org.mpris.MediaPlayer2 methods
    if (std::strcmp(member, "Raise") == 0) {
        // Show the main window — handled by the UI layer.
        return;
    }
    if (std::strcmp(member, "Quit") == 0) {
        // Request application shutdown — handled by the app lifecycle.
        return;
    }

    // org.mpris.MediaPlayer2.Player methods
    if (std::strcmp(member, "Next") == 0) {
        if (on_next_) on_next_();
    } else if (std::strcmp(member, "Previous") == 0) {
        if (on_previous_) on_previous_();
    } else if (std::strcmp(member, "Pause") == 0) {
        if (on_pause_) on_pause_();
    } else if (std::strcmp(member, "Play") == 0) {
        if (on_play_) on_play_();
    } else if (std::strcmp(member, "PlayPause") == 0) {
        if (status_ == PlaybackStatus2::Playing && on_pause_) {
            on_pause_();
        } else if (on_play_) {
            on_play_();
        }
    } else if (std::strcmp(member, "Stop") == 0) {
        if (on_stop_) on_stop_();
    } else if (std::strcmp(member, "Seek") == 0) {
        if (on_seek_) {
            // Parse the offset from the incoming message in the derived class's dispatch.
            // (The offset is extracted during message iteration before calling this.)
        }
    } else if (std::strcmp(member, "SetPosition") == 0) {
        if (on_set_position_) {
            // Parse track-id and position before calling this.
        }
    } else if (std::strcmp(member, "OpenUri") == 0) {
        if (on_open_uri_) {
            // Parse the URI before calling this.
        }
    }
}

}  // namespace arrow::native
