// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// System tray implementation.

#include "native/tray.hpp"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#if __has_include(<libappindicator3/app-indicator.h>)
#include <libappindicator3/app-indicator.h>
#elif __has_include(<ayatana-appindicator3/app-indicator.h>)
#include <ayatana-appindicator3/app-indicator.h>
#endif
#endif

namespace arrow::native {

namespace {

// Build the combined tooltip string.
[[nodiscard]] std::string build_tooltip_text(const TrayTooltip& tooltip) noexcept {
    if (tooltip.title.empty() && tooltip.subtitle.empty()) {
        return "Arrow Player";
    }
    if (tooltip.subtitle.empty()) {
        return tooltip.title;
    }
    return tooltip.title + " — " + tooltip.subtitle;
}

}  // namespace

TrayController::TrayController(TrayCallbacks callbacks) noexcept
    : callbacks_{std::move(callbacks)} {}

TrayController::~TrayController() noexcept {
    destroy();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

[[nodiscard]] bool TrayController::create() noexcept {
    std::unique_lock lock{mutex_};

    if (created_.load(std::memory_order_acquire)) return available_.load(std::memory_order_acquire);

#if defined(_WIN32)
    const bool ok = create_windows();
#elif defined(__linux__)
    const bool ok = create_linux();
#else
    const bool ok = false;
#endif

    available_.store(ok, std::memory_order_release);
    created_.store(true, std::memory_order_release);
    return ok;
}

void TrayController::destroy() noexcept {
    std::unique_lock lock{mutex_};

    if (!created_.load(std::memory_order_acquire)) return;

#if defined(_WIN32)
    destroy_windows();
#elif defined(__linux__)
    destroy_linux();
#endif

    created_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// State updates
// ---------------------------------------------------------------------------

void TrayController::update_tooltip(const TrayTooltip& tooltip) noexcept {
    std::unique_lock lock{mutex_};
    tooltip_ = tooltip;

    if (!available_.load(std::memory_order_acquire)) return;

#if defined(_WIN32)
    update_tooltip_windows(tooltip);
#elif defined(__linux__)
    update_tooltip_linux(tooltip);
#endif
}

void TrayController::update_playback_state(const TrayPlaybackState state) noexcept {
    std::unique_lock lock{mutex_};
    playback_state_ = state;
    // Icon/badge update would be done here per-platform.
    (void)state;
}

void TrayController::update_track(const std::string_view title,
                                 const std::string_view artist) noexcept {
    update_tooltip(TrayTooltip{std::string{title}, std::string{artist}});
}

// ---------------------------------------------------------------------------
// Windows implementation
// ---------------------------------------------------------------------------

#if defined(_WIN32)

namespace {

// Lowest ID for tray menu items.
constexpr int MENU_ID_BASE = 1000;
constexpr int MENU_ID_PLAY_PAUSE = 1001;
constexpr int MENU_ID_NEXT = 1002;
constexpr int MENU_ID_PREV = 1003;
constexpr int MENU_ID_QUIT = 1004;

// Message sent when a tray icon event occurs.
constexpr int WM_TRAY_ICON = WM_USER + 1;

// Menu item data pointer.
struct TrayMenuItemData final {
    TrayController* controller{nullptr};
    int id{0};
};

}  // namespace

bool TrayController::create_windows() noexcept {
    // Find the application window to associate with the tray icon.
    HWND hwnd = GetActiveWindow();
    if (!hwnd) {
        // Try to find any visible Arrow Player window.
        hwnd = FindWindowW(L"ArrowPlayerWindowClass", nullptr);
    }
    if (!hwnd) {
        // No window yet — create an invisible message-only window to own the tray icon.
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"ArrowPlayerTrayHost";
        if (!RegisterClassW(&wc)) return false;

        hwnd = CreateWindowW(L"ArrowPlayerTrayHost", nullptr,
                             WS_OVERLAPPEDWINDOW,
                             0, 0, 0, 0,
                             nullptr, nullptr, nullptr, nullptr);
        if (!hwnd) return false;
    }

    handle_ = reinterpret_cast<void*>(hwnd);

    // Register the window message for tray icon events.
    // The tray icon will send us mouse events via PostMessage.
    SetLastError(0);
    return true;  // Icon creation is deferred to the UI layer which has the icon resource.
}

void TrayController::destroy_windows() noexcept {
    if (handle_) {
        // The UI layer removes the Shell_NotifyIcon on shutdown.
        handle_ = nullptr;
    }
}

void TrayController::update_tooltip_windows(const TrayTooltip& tooltip) noexcept {
    if (!handle_) return;
    // The tooltip is updated by the UI layer when it manages the Shell_NotifyIcon.
    // This method is a no-op when the icon is managed at the UI level.
    (void)tooltip;
}

#endif  // _WIN32

// ---------------------------------------------------------------------------
// Linux implementation
// ---------------------------------------------------------------------------

#if defined(__linux__)

namespace {

// Menu item IDs (shared with libappindicator).
constexpr int APPIND_MENU_PLAY_PAUSE = 0;
constexpr int APPIND_MENU_NEXT = 1;
constexpr int APPIND_MENU_PREV = 2;
constexpr int APPIND_MENU_QUIT = 3;

}  // namespace

bool TrayController::create_linux() noexcept {
    // Try to load libappindicator3.so for a freedesktop-compliant tray.
    void* lib = dlsym(RTLD_DEFAULT, "app_indicator_new");
    if (!lib) {
        // libappindicator not available — try ayatana-appindicator.
        lib = dlsym(RTLD_DEFAULT, "ayatana_app_indicator_new");
    }
    if (!lib) {
        // No system tray library available — graceful degradation (REQ-GEN-003).
        return false;
    }

    // The actual app-indicator creation requires a GTK main context, which belongs
    // to the UI layer.  We signal that the backend is available so the UI layer
    // can create the indicator there.
    // For now we simply report success and defer to the GTK-based UI.
    (void)lib;
    return true;
}

void TrayController::destroy_linux() noexcept {
    // Destruction is handled by the GTK layer.
}

void TrayController::update_tooltip_linux(const TrayTooltip& tooltip) noexcept {
    // Deferred to the GTK layer.
    (void)tooltip;
}

#endif  // __linux__

}  // namespace arrow::native
