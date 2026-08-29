// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// System tray integration — cross-platform tray icon with right-click menu
// and double-click show/hide.
//
/// Graceful degradation if no system tray is available (REQ-GEN-003).
//
// Windows: uses Shell_NotifyIcon API.
// Linux:   uses libappindicator3 or raw X11 + libayatana-appindicator.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace arrow::native {

/// Callbacks from the tray to the application.
struct TrayCallbacks final {
    std::function<void()> on_play_pause;
    std::function<void()> on_next;
    std::function<void()> on_previous;
    std::function<void()> on_show;
    std::function<void()> on_quit;
};

/// Tray tooltip text snapshot.
struct TrayTooltip final {
    std::string title;
    std::string subtitle;
};

/// Playback state used to update the tray icon/badge.
enum class TrayPlaybackState {
    Stopped,
    Playing,
    Paused,
};

/// System tray controller.
///
/// Manages a single tray icon with a right-click context menu and a double-click
/// action.  Gracefully degrades if no system tray is available (logs a warning
/// but does not fail).
class TrayController final {
  public:
    /// Constructs the tray controller.  Callbacks may be null.
    explicit TrayController(TrayCallbacks callbacks) noexcept;
    ~TrayController() noexcept;

    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;
    TrayController(TrayController&&) = delete;
    TrayController& operator=(TrayController&&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Creates the tray icon.  Returns true on success, false if no tray
    /// is available (logged as a warning per REQ-GEN-003).
    [[nodiscard]] bool create() noexcept;

    /// Destroys the tray icon.
    void destroy() noexcept;

    // -----------------------------------------------------------------------
    // State updates
    // -----------------------------------------------------------------------

    /// Updates the tooltip text (visible on hover).
    void update_tooltip(const TrayTooltip& tooltip) noexcept;

    /// Updates the playback state (used to choose the icon/badge).
    void update_playback_state(TrayPlaybackState state) noexcept;

    /// Updates the current track info in the tooltip.
    void update_track(std::string_view title, std::string_view artist) noexcept;

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    [[nodiscard]] bool is_available() const noexcept {
        return available_.load(std::memory_order_acquire);
    }

  private:
    // Platform implementations.
#if defined(_WIN32)
    bool create_windows() noexcept;
    void destroy_windows() noexcept;
    void update_tooltip_windows(const TrayTooltip& tooltip) noexcept;
#elif defined(__linux__)
    bool create_linux() noexcept;
    void destroy_linux() noexcept;
    void update_tooltip_linux(const TrayTooltip& tooltip) noexcept;
#else
    bool create_generic() noexcept { return false; }
    void destroy_generic() noexcept {}
    void update_tooltip_generic(const TrayTooltip&) noexcept {}
#endif

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    mutable std::mutex mutex_;
    TrayCallbacks callbacks_;
    TrayTooltip tooltip_{};
    TrayPlaybackState playback_state_{TrayPlaybackState::Stopped};
    std::atomic<bool> available_{false};
    std::atomic<bool> created_{false};

    // Platform handle (Windows: HWND + NotifyIconData; Linux: appindicator handle).
    void* handle_{nullptr};
};

}  // namespace arrow::native
