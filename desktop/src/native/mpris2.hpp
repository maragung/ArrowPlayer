// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MPRIS2 D-Bus interface — org.mpris.MediaPlayer2.arrowplayer.
// Implements the full MediaPlayer2 and MediaPlayer2.Player interfaces per
// the MPRIS2 specification (https://specifications.freedesktop.org/mpris-spec/latest/).
//
// Properties: PlaybackStatus, LoopStatus, Shuffle, Volume, Position, Metadata.
// Methods:     Next, Previous, Pause, Play, Stop, Seek, SetPosition, OpenUri.
// Signals:     Seeked, PropertiesChanged.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace arrow::native {

/// Loop mode mirroring MPRIS2 LoopStatus.
enum class LoopStatus {
    None,
    Track,
    Playlist,
};

/// Playback state mirroring MPRIS2 PlaybackStatus.
enum class PlaybackStatus2 {
    Stopped,
    Playing,
    Paused,
};

/// Track metadata map (MPRIS2 Metadata dict: string → variant).
/// Supported keys: "mpris:trackid", "xesam:title", "xesam:artist",
/// "xesam:album", "mpris:length", "mpris:artUrl", "xesam:url".
using MprisMetadata = std::unordered_map<std::string, std::variant<std::string, std::int64_t, double>>;

/// Callback signatures mirroring MPRIS2 player methods.
using MprisNextCallback = std::function<void()>;
using MprisPreviousCallback = std::function<void()>;
using MprisPauseCallback = std::function<void()>;
using MprisPlayCallback = std::function<void()>;
using MprisStopCallback = std::function<void()>;
using MprisSeekCallback = std::function<void(std::chrono::microseconds offset)>;
using MprisSetPositionCallback = std::function<void(std::string_view track_id, std::chrono::microseconds position)>;
using MprisOpenUriCallback = std::function<void(std::string_view uri)>;

/// MPRIS2 controller.
///
/// Owns the D-Bus connection and the org.mpris.MediaPlayer2.arrowplayer bus name.
/// Thread-safe: all mutating calls lock internally.
/// The D-Bus main loop runs on an internal thread.
class MprisController final {
  public:
    /// Constructs the controller and registers the bus name, but does NOT
    /// claim the name until claim() is called.
    explicit MprisController() noexcept;
    ~MprisController() noexcept;

    MprisController(const MprisController&) = delete;
    MprisController& operator=(const MprisController&) = delete;
    MprisController(MprisController&&) = delete;
    MprisController& operator=(MprisController&&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Claims the org.mpris.MediaPlayer2.arrowplayer bus name.
    /// Starts the D-Bus main loop thread.
    /// Returns false if the bus name is already taken.
    [[nodiscard]] bool claim() noexcept;

    /// Releases the bus name and stops the D-Bus loop.
    void release() noexcept;

    /// Returns true if the bus name is currently held.
    [[nodiscard]] bool is_active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    // -----------------------------------------------------------------------
    // Player callbacks — set these before claiming the bus name.
    // -----------------------------------------------------------------------
    void set_callbacks(MprisNextCallback,
                       MprisPreviousCallback,
                       MprisPauseCallback,
                       MprisPlayCallback,
                       MprisStopCallback,
                       MprisSeekCallback,
                       MprisSetPositionCallback,
                       MprisOpenUriCallback) noexcept;

    // -----------------------------------------------------------------------
    // Player state (pushed by the application)
    // -----------------------------------------------------------------------

    /// Updates the full playback state exposed to D-Bus clients.
    void update_state(PlaybackStatus2 status,
                      std::chrono::microseconds position,
                      double volume,
                      LoopStatus loop,
                      bool shuffle,
                      const MprisMetadata& metadata) noexcept;

    /// Emits the Seeked signal.
    void emit_seeked(std::chrono::microseconds new_position) noexcept;

    // -----------------------------------------------------------------------
    // MPRIS2 property getters (for introspection)
    // -----------------------------------------------------------------------
    [[nodiscard]] PlaybackStatus2 playback_status() const noexcept;
    [[nodiscard]] LoopStatus loop_status() const noexcept;
    [[nodiscard]] double volume() const noexcept;
    [[nodiscard]] std::chrono::microseconds position() const noexcept;
    [[nodiscard]] MprisMetadata metadata() const noexcept;
    [[nodiscard]] bool can_go_next() const noexcept;
    [[nodiscard]] bool can_go_previous() const noexcept;
    [[nodiscard]] bool can_play() const noexcept;
    [[nodiscard]] bool can_pause() const noexcept;
    [[nodiscard]] bool can_seek() const noexcept;
    [[nodiscard]] bool can_control() const noexcept;

  private:
    // -----------------------------------------------------------------------
    // D-Bus dispatch helpers (called from the mainloop thread)
    // -----------------------------------------------------------------------
    void mainloop() noexcept;
    void on_method_call(const char* interface,
                        const char* member,
                        std::uint32_t serial) noexcept;
    void on_get_property(std::uint32_t serial,
                         const char* property_name) noexcept;
    void on_get_all_properties(std::uint32_t serial) noexcept;

    // D-Bus message builders.
    static void build_property_reply(void* ctx,
                                     const char* name,
                                     const char* signature,
                                     const void* value) noexcept;
    static void build_all_properties_reply(void* ctx, std::uint32_t serial) noexcept;

    // -----------------------------------------------------------------------
    // Internal state
    // -----------------------------------------------------------------------
    mutable std::mutex mutex_;

    std::atomic<bool> active_{false};

    PlaybackStatus2 status_{PlaybackStatus2::Stopped};
    LoopStatus loop_{LoopStatus::None};
    double volume_{1.0};
    std::chrono::microseconds position_{0};
    MprisMetadata metadata_;

    std::chrono::microseconds position_at_last_update_{0};
    std::chrono::steady_clock::time_point last_update_time_;

    MprisNextCallback on_next_;
    MprisPreviousCallback on_previous_;
    MprisPauseCallback on_pause_;
    MprisPlayCallback on_play_;
    MprisStopCallback on_stop_;
    MprisSeekCallback on_seek_;
    MprisSetPositionCallback on_set_position_;
    MprisOpenUriCallback on_open_uri_;

    // D-Bus connection state.
    void* connection_{nullptr};  // DBusConnection*
    std::thread mainloop_thread_;
    std::atomic<bool> mainloop_running_{false};

    // Pending reply contexts (serial → reply builder).
    struct PendingReply final {
        std::uint32_t serial{0};
        std::chrono::steady_clock::time_point deadline;
        void (*builder)(void*, std::uint32_t){nullptr};
        void* builder_ctx{nullptr};
    };
    std::vector<PendingReply> pending_replies_;
};

}  // namespace arrow::native
