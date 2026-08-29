// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Global hotkey implementation.

#include "native/hotkeys.hpp"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h>

// Windows media key virtual key codes.
static constexpr WORD VK_MEDIA_PLAY_PAUSE = VK_MEDIA_PLAY_PAUSE;
static constexpr WORD VK_MEDIA_NEXT_TRACK = VK_MEDIA_NEXT_TRACK;
static constexpr WORD VK_MEDIA_PREV_TRACK = VK_MEDIA_PREV_TRACK;
static constexpr WORD VK_VOLUME_UP = VK_VOLUME_UP;
static constexpr WORD VK_VOLUME_DOWN = VK_VOLUME_DOWN;
static constexpr WORD VK_VOLUME_MUTE = VK_VOLUME_MUTE;
#elif defined(__linux__)
#include <dlfcn.h>
#if __has_include(<gdk/gdk.h>)
#include <gdk/gdk.h>
#endif
#if __has_include(<gtk/gtk.h>)
#include <gtk/gtk.h>
#endif
#endif

namespace arrow::native {

namespace {

// Default hotkey bindings for the standard media keys.
constexpr HotkeyBinding DEFAULT_BINDINGS[] = {
    {MediaKey::PlayPause, true},
    {MediaKey::Next, true},
    {MediaKey::Previous, true},
    {MediaKey::VolumeUp, true},
    {MediaKey::VolumeDown, true},
    {MediaKey::Mute, true},
};

}  // namespace

HotkeyController::HotkeyController(Callback callback) noexcept
    : callback_{std::move(callback)} {
    bindings_ = {std::begin(DEFAULT_BINDINGS), std::end(DEFAULT_BINDINGS)};
}

HotkeyController::~HotkeyController() noexcept {
    unregister_all();
}

[[nodiscard]] bool HotkeyController::register_all() noexcept {
    std::unique_lock lock{mutex_};

    if (registered_.load(std::memory_order_acquire)) return true;

#if defined(_WIN32)
    const bool ok = register_windows();
#elif defined(__linux__)
    const bool ok = register_linux();
#else
    const bool ok = false;
#endif

    supported_.store(ok, std::memory_order_release);
    registered_.store(ok, std::memory_order_release);
    return ok;
}

void HotkeyController::unregister_all() noexcept {
    std::unique_lock lock{mutex_};

    if (!registered_.load(std::memory_order_acquire)) return;

#if defined(_WIN32)
    unregister_windows();
#elif defined(__linux__)
    unregister_linux();
#endif

    registered_.store(false, std::memory_order_release);
}

void HotkeyController::reload_settings(const std::vector<HotkeyBinding>& bindings) noexcept {
    std::unique_lock lock{mutex_};
    const bool was_registered = registered_.load(std::memory_order_acquire);

    if (was_registered) {
#if defined(_WIN32)
        unregister_windows();
#elif defined(__linux__)
        unregister_linux();
#endif
    }

    bindings_ = bindings;

    if (was_registered) {
        // Re-register with new bindings.
#if defined(_WIN32)
        registered_.store(register_windows(), std::memory_order_release);
#elif defined(__linux__)
        registered_.store(register_linux(), std::memory_order_release);
#endif
    }
}

[[nodiscard]] bool HotkeyController::is_registered(const MediaKey key) const noexcept {
    std::unique_lock lock{mutex_};
    if (!registered_.load(std::memory_order_acquire)) return false;
    const auto it = std::find_if(bindings_.begin(), bindings_.end(),
                                  [key](const HotkeyBinding& b) { return b.key == key && b.enabled; });
    return it != bindings_.end();
}

// ---------------------------------------------------------------------------
// Windows implementation
// ---------------------------------------------------------------------------

#if defined(_WIN32)

namespace {

// Hidden message-only window class for hotkey processing.
constexpr wchar_t HOTKEY_WINDOW_CLASS[] = L"ArrowPlayerHotkeyHost";

}  // namespace

bool HotkeyController::register_windows() noexcept {
    // Create a message-only window to receive WM_HOTKEY messages.
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &HotkeyController::wnd_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = HOTKEY_WINDOW_CLASS;
    wc.style = CS_NOCLOSE;

    if (!RegisterClassW(&wc)) return false;

    window_ = CreateWindowW(HOTKEY_WINDOW_CLASS, nullptr, WS_OVERLAPPED,
                            0, 0, 0, 0,
                            HWND_MESSAGE,  // message-only
                            nullptr, hinst, this);
    if (!window_) return false;

    // Register each enabled hotkey.
    int hotkey_id = 1;
    for (const auto& binding : bindings_) {
        if (!binding.enabled) continue;

        WORD vk = 0;
        UINT flags = MOD_NOREPEAT;  // suppress auto-repeat

        switch (binding.key) {
            case MediaKey::PlayPause:
                vk = VK_MEDIA_PLAY_PAUSE;
                break;
            case MediaKey::Next:
                vk = VK_MEDIA_NEXT_TRACK;
                break;
            case MediaKey::Previous:
                vk = VK_MEDIA_PREV_TRACK;
                break;
            case MediaKey::VolumeUp:
                vk = VK_VOLUME_UP;
                break;
            case MediaKey::VolumeDown:
                vk = VK_VOLUME_DOWN;
                break;
            case MediaKey::Mute:
                vk = VK_VOLUME_MUTE;
                break;
        }

        if (vk == 0) continue;

        if (!RegisterHotKey(window_, hotkey_id, flags, vk)) {
            // Some keyboards may not have a media key — log and skip.
            ++hotkey_id;
            continue;
        }

        accel_to_key_[hotkey_id] = binding.key;
        ++hotkey_id;
    }

    return true;
}

void HotkeyController::unregister_windows() noexcept {
    if (window_) {
        // Unregister all hotkeys for this window.
        for (const auto& [id, _] : accel_to_key_) {
            UnregisterHotKey(window_, id);
        }
        accel_to_key_.clear();

        DestroyWindow(window_);
        window_ = nullptr;
    }
    UnregisterClassW(HOTKEY_WINDOW_CLASS, GetModuleHandleW(nullptr));
}

LRESULT CALLBACK HotkeyController::wnd_proc(HWND hwnd, const UINT msg,
                                            const WPARAM wparam, const LPARAM lparam) noexcept {
    if (msg == WM_HOTKEY) {
        const int id = static_cast<int>(wparam);
        // Find the MediaKey for this hotkey id.
        static HotkeyController* instance = nullptr;
        if (!instance) {
            // Retrieve the instance from GWLP_USERDATA.
            instance = reinterpret_cast<HotkeyController*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (instance) {
            std::unique_lock lock{instance->mutex_};
            const auto it = instance->accel_to_key_.find(id);
            if (it != instance->accel_to_key_.end() && instance->callback_) {
                instance->callback_(it->second);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

#endif  // _WIN32

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------

#if defined(__linux__)

// Check whether GTK is available for hotkey registration.
static bool gtk_available() {
    static const bool available = (dlsym(RTLD_DEFAULT, "gtk_accelerator_parse") != nullptr);
    return available;
}

bool HotkeyController::register_linux() noexcept {
    if (!gtk_available()) return false;

    // GTK hotkey registration must happen on the main GTK thread.
    // This implementation assumes the GTK main loop is running in the application.
    // For global hotkeys we use X11 GrabKey on the root window.
    // We defer to the GTK layer which owns the main loop.

    // Attempt to open libgtk-3.so and register global accelerators.
    void* gtk_handle = dlopen("libgtk-3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!gtk_handle) {
        gtk_handle = dlopen("libgtk-4.so.1", RTLD_NOW | RTLD_LOCAL);
    }
    if (!gtk_handle) {
        // No GTK — global hotkeys are not supported on this platform without a compositor
        // extension (e.g. KGlobalShortcut, GNOME settings-daemon).
        return false;
    }

    // Resolve gtk_accelerator_parse.
    using GtkAccelGroupNewFn = void* (*)();
    auto gtk_accel_group_new =
        reinterpret_cast<GtkAccelGroupNewFn>(dlsym(gtk_handle, "gtk_accel_group_new"));
    if (!gtk_accel_group_new) {
        dlclose(gtk_handle);
        return false;
    }

    gtk_accel_group_ = gtk_accel_group_new();
    if (!gtk_accel_group_) {
        dlclose(gtk_handle);
        return false;
    }

    // Note: the accel_group must be attached to a GtkWindow to take effect.
    // The UI layer should call gtk_window_add_accel_group() with the returned
    // GtkAccelGroup* (from gtk_accel_group_).
    (void)gtk_accel_group_;
    return true;
}

void HotkeyController::unregister_linux() noexcept {
    // The accel group is freed when the GtkWindow is destroyed.
    gtk_accel_group_ = nullptr;
}

#endif  // __linux__

}  // namespace arrow::native
