// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Global media-key hotkey registration — cross-platform.
// Registers media key shortcuts (play/pause, next, prev, vol up/down) at the OS
// level so they work even when the application is not focused.
// Per SPEC §14.3, REQ-OSI-020..REQ-OSI-029.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arrow::native {

/// Standard media key identifiers.
enum class MediaKey {
    PlayPause,
    Next,
    Previous,
    VolumeUp,
    VolumeDown,
    Mute,
};

/// A hotkey binding: key → action.
struct HotkeyBinding final {
    MediaKey key{MediaKey::PlayPause};
    bool enabled{true};
};

/// Global hotkey controller.
///
/// Manages OS-level media key registrations.  Thread-safe.
/// Gracefully degrades if the platform does not support global hotkeys.
class HotkeyController final {
  public:
    using Callback = std::function<void(MediaKey key)>;

    /// Constructs the controller with a callback invoked on any media key press.
    explicit HotkeyController(Callback callback) noexcept;
    ~HotkeyController() noexcept;

    HotkeyController(const HotkeyController&) = delete;
    HotkeyController& operator=(const HotkeyController&) = delete;
    HotkeyController(HotkeyController&&) = delete;
    HotkeyController& operator=(HotkeyController&&) = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Registers all known media key bindings with the OS.
    /// Returns true if registration succeeded (may be partial on some platforms).
    [[nodiscard]] bool register_all() noexcept;

    /// Unregisters all media key bindings.
    void unregister_all() noexcept;

    /// Re-reads settings and updates registrations.
    void reload_settings(const std::vector<HotkeyBinding>& bindings) noexcept;

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    [[nodiscard]] bool is_supported() const noexcept {
        return supported_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_registered(MediaKey key) const noexcept;

  private:
#if defined(_WIN32)
    bool register_windows() noexcept;
    void unregister_windows() noexcept;
    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept;
#elif defined(__linux__)
    bool register_linux() noexcept;
    void unregister_linux() noexcept;
    static void handle_gtk_accel(const char* path, void* user_data) noexcept;
#else
    bool register_generic() noexcept { return false; }
    void unregister_generic() noexcept {}
#endif

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    mutable std::mutex mutex_;
    Callback callback_;

    std::atomic<bool> supported_{false};
    std::atomic<bool> registered_{false};

    std::vector<HotkeyBinding> bindings_;

    // Platform-specific handle.
#if defined(_WIN32)
    HWND window_{nullptr};
    HACCEL accelerator_table_{nullptr};
    std::unordered_map<int, MediaKey> accel_to_key_;
#elif defined(__linux__)
    void* gtk_accel_group_{nullptr};  // GtkAccelGroup*
    std::unordered_map<std::uint32_t, MediaKey> keyval_to_media_key_;
#endif
};

/// Converts a MediaKey to a human-readable name.
[[nodiscard]] constexpr std::string_view to_string(MediaKey key) noexcept {
    switch (key) {
        case MediaKey::PlayPause:
            return "Play/Pause";
        case MediaKey::Next:
            return "Next Track";
        case MediaKey::Previous:
            return "Previous Track";
        case MediaKey::VolumeUp:
            return "Volume Up";
        case MediaKey::VolumeDown:
            return "Volume Down";
        case MediaKey::Mute:
            return "Mute";
    }
    return "Unknown";
}

}  // namespace arrow::native
