// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Windows System Tray Media Control (SMTC) integration.
// Exposes playback state, track metadata, and media key controls via the
// Windows::Media::Control global media control API (ICoreWindow /
// GlobalSystemMediaTransportControlsSessionManager).
//
// Usage:
//   auto smtc = std::make_unique<SmtcController>();
//   smtc->update_metadata({.title = L"Track", .artist = L"Artist", .album = L"Album"});
//   smtc->update_playback(SmtcPlaybackState::Playing, 0ms, 180000ms);

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace arrow::native {

/// Playback state forwarded to the SMTC UI surface.
enum class SmtcPlaybackState {
    Stopped,
    Playing,
    Paused,
};

/// Track metadata displayed in the SMTC flyout.
struct SmtcMediaMetadata final {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring thumbnail_path;  // absolute path; empty = no art
    std::chrono::milliseconds duration{0};
    std::chrono::milliseconds position{0};
};

/// Media key button identifiers.
enum class SmtcButton {
    PlayPause,
    Next,
    Previous,
    Stop,
    FastForward,
    Rewind,
};

/// Callback invoked when the user presses a media button in the SMTC flyout.
/// The receiver should update the player and call update_playback() to
/// reflect the new state.
using SmtcButtonPressedCallback = std::function<void(SmtcButton button)>;

/// Windows SMTC controller.
///
/// Provides a thread-safe interface to the Windows global media control surface.
/// Construction is cheap and does not open the system session; call initialize()
/// to acquire the session handle.
class SmtcController final {
  public:
    /// Constructs the controller.  button_callback will be invoked on the
    /// SMTC async callback thread — avoid blocking or allocating in it.
    explicit SmtcController(SmtcButtonPressedCallback button_callback) noexcept;
    ~SmtcController() noexcept;

    SmtcController(const SmtcController&) = delete;
    SmtcController& operator=(const SmtcController&) = delete;
    SmtcController(SmtcController&&) = delete;
    SmtcController& operator=(SmtcController&&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Acquires the current GlobalSystemMediaTransportControlsSession.
    /// Safe to call multiple times; subsequent calls are no-ops if already initialized.
    [[nodiscard]] bool initialize() noexcept;

    /// Releases the session handle.
    void shutdown() noexcept;

    // -----------------------------------------------------------------------
    // Playback state (exposed to the system)
    // -----------------------------------------------------------------------

    /// Updates the playback state shown in the SMTC flyout.
    void update_playback(SmtcPlaybackState state,
                        std::chrono::milliseconds position,
                        std::chrono::milliseconds duration) noexcept;

    /// Updates the track metadata (title, artist, album, art, duration).
    void update_metadata(const SmtcMediaMetadata& metadata) noexcept;

    /// Clears all displayed metadata (shown when playback is stopped).
    void clear() noexcept;

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /// True if the SMTC session is currently active.
    [[nodiscard]] bool is_active() const noexcept {
        return session_acquired_.load(std::memory_order_acquire);
    }

  private:
    // Static entry point for the async button callback thread.
    static DWORD WINAPI button_callback_trampoline(LPVOID lpParameter) noexcept;

    // Internal: polls or waits for the session.
    [[nodiscard]] bool wait_for_session() noexcept;

    // Internal: sends the playback info to the system.
    void apply_playback_state(SmtcPlaybackState state,
                              std::chrono::milliseconds position,
                              std::chrono::milliseconds duration) noexcept;

    // Internal: sends the media info to the system.
    void apply_media_info(const SmtcMediaMetadata& metadata) noexcept;

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    mutable std::mutex mutex_;

    // COM session handle (Windows Runtime / WinRT).
    void* session_{nullptr};  // GlobalSystemMediaTransportControlsSession
    void* session_manager_{nullptr};  // GlobalSystemMediaTransportControlsSessionManager
    void* interop_{nullptr};  // IPropertySet

    bool session_acquired_{false};

    // Cached metadata for idempotent updates.
    SmtcMediaMetadata last_metadata_;
    SmtcPlaybackState last_state_{SmtcPlaybackState::Stopped};

    // Button callback (runs on the SMTC async thread).
    SmtcButtonPressedCallback on_button_pressed_;

    // Thread handle for the async callback.
    HANDLE callback_thread_{nullptr};
    HANDLE callback_stop_{nullptr};  // event to signal thread shutdown
};

}  // namespace arrow::native
