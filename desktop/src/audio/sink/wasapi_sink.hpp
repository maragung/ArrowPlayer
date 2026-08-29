// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// WASAPI audio sink — REQ-AUD-118 device-loss recovery, REQ-AUD-119 exclusive mode,
// REQ-AUD-120 shared mode, REQ-AUD-067 device enumeration.
// Uses raw COM (no C++/WinRT dependency) for build-system simplicity.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "audio/ports/audio_ports.hpp"
#include "core/error.hpp"

// -----------------------------------------------------------------------
// WASAPI / Windows SDK types — only available on Windows
// -----------------------------------------------------------------------
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wil/com.h>
#include <windows.h>
#endif

namespace arrow::audio {

#if defined(_WIN32)

// Forward declare Windows COM interfaces.
struct IAudioClient;
struct IAudioRenderClient;
struct IMMDevice;
struct IMMDeviceEnumerator;
struct IPropertyStore;
struct IUnknown;
struct PROPERTYKEY;

// WIL com_ptr alias for readability.
template<typename T>
using ComPtr = wil::com_ptr<T>;

// Callback invoked on the render thread when the device is lost or changed.
// RT-SAFE: may be called from the audio RT thread — must not block or allocate.
using DeviceChangeCallback = std::function<void()>;

/// Maps a WASAPI event-driven index to a poll/notify handle.
struct PollHandle final {
    HANDLE event{nullptr};  // WIN32 event signalled by AUDCLNT_STREAMFLAGS_EVENTCALLBACK
};

/// Internal playback state machine for REQ-AUD-118.
enum class SinkState {
    Closed,
    Open,
    Started,
    Recovering,
};

/// WASAPI playback device info for enumeration.
struct WasapiDevice final {
    std::wstring id;
    std::wstring name;
    bool is_default{false};
};

/// WASAPI audio sink.
///
/// Supports shared mode (default) and exclusive mode (bit-perfect).
/// Implements IAudioSink against IAudioClient / IAudioClient3.
///
/// Device-change notifications via IMMNotificationClient allow the caller to
/// react to default-device switches and physical device arrivals/removals.
class WasapiSink final : public IAudioSink {
  public:
    explicit WasapiSink(DeviceChangeCallback on_device_change = nullptr) noexcept;
    ~WasapiSink() noexcept override;

    WasapiSink(const WasapiSink&) = delete;
    WasapiSink& operator=(const WasapiSink&) = delete;
    WasapiSink(WasapiSink&&) = delete;
    WasapiSink& operator=(WasapiSink&&) = delete;

    // IAudioSink interface
    [[nodiscard]] Result<SinkConfig> open(const SinkConfig& requested) override;
    [[nodiscard]] Status start() override;
    void stop() noexcept override;
    void close() noexcept override;
    [[nodiscard]] Status write(PlanarFrames frames) noexcept override;
    [[nodiscard]] std::string_view device_name() const noexcept override;

    // WASAPI-specific API
    [[nodiscard]] std::vector<WasapiDevice> enumerate_devices() const;
    [[nodiscard]] Status recover();
    void set_device_id(std::wstring id);
    [[nodiscard]] const std::wstring& current_device_name() const noexcept {
        return device_name_;
    }
    [[nodiscard]] std::size_t written_frames() const noexcept {
        return written_frames_.load(std::memory_order_relaxed);
    }

  private:
    [[nodiscard]] static HRESULT create_device_enumerator(IMMDeviceEnumerator** out) noexcept;
    [[nodiscard]] HRESULT get_device_by_id(IMMDevice** out, const WCHAR* id) const noexcept;
    [[nodiscard]] HRESULT get_default_device(IMMDevice** out, EDataFlow flow,
                                             DWORD role) const noexcept;
    [[nodiscard]] Result<SinkConfig> open_device(IMMDevice* device,
                                                  const SinkConfig& requested);
    [[nodiscard]] Result<SinkConfig> try_init_audio_client(IAudioClient* client,
                                                            const SinkConfig& requested,
                                                            bool exclusive);

    // RT thread entry point.
    void render_loop(std::jthread::stop_token token) noexcept;

    // IMMNotificationClient implementation (raw COM).
    class DeviceNotifier;

    // COM objects (non-owning while started, released on close).
    ComPtr<IMMDeviceEnumerator> enumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audio_client_;
    ComPtr<IAudioRenderClient> render_client_;
    ComPtr<DeviceNotifier> notifier_;

    // Render thread.
    std::jthread render_thread_;

    // Device identification.
    std::wstring device_id_;
    std::wstring device_name_;
    std::atomic<std::size_t> written_frames_{0};

    // Configuration.
    SinkConfig config_{};
    std::atomic<SinkState> state_{SinkState::Closed};

    // Event-driven notification handle.
    PollHandle poll_handle_;

    // User-supplied callback.
    DeviceChangeCallback on_device_change_;
};

// ---------------------------------------------------------------------------
// Device notification sink (raw IMMNotificationClient)
// ---------------------------------------------------------------------------
class WasapiSink::DeviceNotifier : public IMMNotificationClient {
  public:
    explicit DeviceNotifier(WasapiSink& owner) noexcept : owner_{owner} {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override;
    IFACEMETHODIMP_(ULONG) AddRef() noexcept override { return InterlockedIncrement(&refs_); }
    IFACEMETHODIMP_(ULONG) Release() noexcept override {
        return InterlockedDecrement(&refs_);
    }

    IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                          LPCWSTR device_id) noexcept override;
    IFACEMETHODIMP OnDeviceAdded(LPCWSTR device_id) noexcept override;
    IFACEMETHODIMP OnDeviceRemoved(LPCWSTR device_id) noexcept override;
    IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR device_id, DWORD new_state) noexcept override;
    IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR device_id,
                                          const PROPERTYKEY& key) noexcept override;

  private:
    std::atomic<long> refs_{1};
    std::reference_wrapper<WasapiSink> owner_;
};

#endif  // _WIN32

}  // namespace arrow::audio
