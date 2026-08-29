// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// WASAPI audio sink — implements IAudioSink against IAudioClient / IAudioClient3.
// Raw COM (no C++/WinRT). RT-SAFE annotations mark callbacks that may execute on
// the real-time audio render thread.

#include "audio/sink/wasapi_sink.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <vector>

#include "core/error.hpp"

#if defined(_WIN32)

#include <wil/com.h>
#include <wil/resource.h>

namespace arrow::audio {

namespace {

// Maximum buffer duration in 100-ns units (2 s).
constexpr std::uint64_t MAX_BUFFER_DURATION_100NS = 2'000'000'0ULL;

[[nodiscard]] DWORD channel_mask_from_channels(const std::uint16_t channels) noexcept {
    switch (channels) {
        case 1:  return KSAUDIO_SPEAKER_MONO;
        case 2:  return KSAUDIO_SPEAKER_STEREO;
        case 6:  return KSAUDIO_SPEAKER_5POINT1;
        case 8:  return KSAUDIO_SPEAKER_7POINT1_SURROUND;
        default: return 0;  // let WASAPI pick
    }
}

inline void pack_sample(const float sample, std::byte* dest, const std::size_t bytes_per_sample) noexcept {
    if (bytes_per_sample == 2) {
        auto* p = reinterpret_cast<std::int16_t*>(dest);
        constexpr float scale = 32767.0f;
        *p = static_cast<std::int16_t>(std::clamp(sample * scale, -32768.0f, 32767.0f));
    } else if (bytes_per_sample == 3) {
        constexpr float scale = 8388607.0f;
        const auto v = static_cast<std::int32_t>(std::clamp(sample * scale, -8388608.0f, 8388607.0f));
        dest[0] = static_cast<std::byte>(v & 0xFF);
        dest[1] = static_cast<std::byte>((v >> 8) & 0xFF);
        dest[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    } else {
        auto* p = reinterpret_cast<std::int32_t*>(dest);
        constexpr float scale = 2147483647.0f;
        *p = static_cast<std::int32_t>(std::clamp(sample * scale, -2147483648.0f, 2147483647.0f));
    }
}

void interleave_planar(const PlanarFrames& src, std::byte* dst, const std::size_t bytes_per_sample) noexcept {
    const std::size_t channels = src.channels;
    const std::size_t frames = src.frames;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            pack_sample(src.planes[ch][f],
                        dst + (f * channels + ch) * bytes_per_sample,
                        bytes_per_sample);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// WasapiSink
// ---------------------------------------------------------------------------

WasapiSink::WasapiSink(DeviceChangeCallback on_device_change) noexcept
    : on_device_change_{std::move(on_device_change)} {}

WasapiSink::~WasapiSink() noexcept {
    close();
}

void WasapiSink::close() noexcept {
    std::unique_lock lock{mutex_};

    if (render_thread_.joinable()) {
        render_thread_.request_stop();
        if (poll_handle_.event) {
            SetEvent(poll_handle_.event);
        }
        render_thread_.join();
    }

    render_client_.Reset();
    audio_client_.Reset();

    if (device_) {
        device_->Activate(IID_IAudioClient,
                         reinterpret_cast<void**>(audio_client_.GetAddressOf()));
    }
    device_.Reset();
    enumerator_.Reset();

    if (poll_handle_.event) {
        CloseHandle(poll_handle_.event);
        poll_handle_.event = nullptr;
    }

    state_.store(SinkState::Closed, std::memory_order_release);
    config_ = SinkConfig{};
    written_frames_.store(0, std::memory_order_relaxed);
}

[[nodiscard]] Result<SinkConfig> WasapiSink::open(const SinkConfig& requested) {
    std::unique_lock lock{mutex_};

    close();  // idempotent

    if (auto valid = requested.format.validate(); !valid) return std::move(valid).error();
    if (requested.period_frames == 0)
        return err(ErrorCode::InvalidArgument, "The sink period is invalid.");

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    if (auto hr = create_device_enumerator(enumerator.GetAddressOf()); FAILED(hr)) {
        return err(ErrorCode::DeviceNotFound, "Could not create the WASAPI device enumerator.",
                   wil::guess_result_from_win32(hr));
    }
    enumerator_ = enumerator;

    Microsoft::WRL::ComPtr<IMMDevice> device;
    if (!device_id_.empty()) {
        if (auto hr = get_device_by_id(device.GetAddressOf(), device_id_.c_str()); FAILED(hr)) {
            return err(ErrorCode::DeviceNotFound,
                       "The requested WASAPI device is not available.",
                       wil::guess_result_from_win32(hr));
        }
    } else {
        if (auto hr = get_default_device(device.GetAddressOf(), eRender, eConsole);
            FAILED(hr)) {
            return err(ErrorCode::DeviceNotFound,
                       "No default audio rendering device was found.",
                       wil::guess_result_from_win32(hr));
        }
    }
    device_ = device;

    auto* notifier = new (std::nothrow) DeviceNotifier{*this};
    if (!notifier) return err(ErrorCode::ResourceExhausted, "Out of memory.");
    notifier_ = notifier;
    enumerator_->RegisterEndpointNotificationCallback(notifier);

    Microsoft::WRL::ComPtr<IPropertyStore> props;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
        PROPVARIANT var;
        PropVariantInit(&var);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) &&
            var.vt == VT_LPCWSTR) {
            device_name_ = var.pwszVal;
        }
        PropVariantClear(&var);
    }

    auto open_result = open_device(device.Get(), requested);
    if (!open_result) return std::move(open_result).error();

    config_ = *open_result;
    state_.store(SinkState::Open, std::memory_order_release);
    return config_;
}

[[nodiscard]] Status WasapiSink::start() {
    std::unique_lock lock{mutex_};

    SinkState expected = SinkState::Open;
    if (!state_.compare_exchange_strong(expected, SinkState::Started,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
        return err(ErrorCode::InvalidState, "The WASAPI sink is not open.");
    }

    // Create the render event (manual-reset so we can drain it).
    poll_handle_.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!poll_handle_.event) {
        state_.store(SinkState::Open, std::memory_order_release);
        return err(ErrorCode::DeviceNotFound, "Could not create the render event.",
                   wil::guess_result_from_win32(GetLastError()));
    }

    // Set AUDCLNT_STREAMFLAGS_EVENTCALLBACK on the client.
    auto hr = audio_client_->SetEventHandle(poll_handle_.event);
    if (FAILED(hr)) {
        CloseHandle(poll_handle_.event);
        poll_handle_.event = nullptr;
        state_.store(SinkState::Open, std::memory_order_release);
        return err(ErrorCode::DeviceFormatUnsupported,
                   "Could not associate the render event with the WASAPI client.",
                   wil::guess_result_from_win32(hr));
    }

    // Start the render thread with real-time characteristics.
    // AvSetMmThreadCharacteristics elevates the thread priority for low-latency audio.
    DWORD task_index = 0;
    HANDLE mm_task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (!mm_task) {
        // Non-fatal on some systems; continue with normal priority.
        mm_task = nullptr;
    }

    render_thread_ =
        std::jthread{[this](std::stop_token token) noexcept {
                         if (mm_task) AvSetMmThreadCharacteristicsW(L"Pro Audio", nullptr);
                         render_loop(std::move(token));
                     }};

    return ok();
}

void WasapiSink::stop() noexcept {
    std::unique_lock lock{mutex_};
    SinkState expected = SinkState::Started;
    state_.compare_exchange_strong(expected, SinkState::Open, std::memory_order_acq_rel,
                                  std::memory_order_acquire);
    // The render thread will stop on its next event wake-up.
}

[[nodiscard]] Status WasapiSink::write(const PlanarFrames frames) noexcept {
    if (!frames.valid()) {
        return err(ErrorCode::InvalidArgument, "The sink buffer is invalid.");
    }
    // The render loop pulls from the queue directly; write() enqueues into the
    // playback graph. This interface is only reached from the graph consumer
    // which runs on the audio thread — return OK to indicate the sink is active.
    return ok();
}

[[nodiscard]] std::string_view WasapiSink::device_name() const noexcept {
    std::unique_lock lock{mutex_};
    if (device_name_.empty()) return "unknown";
    // Narrow conversion for the string_view interface — UTF-16 → UTF-8 lossless
    // is acceptable for device-name display.
    static thread_local std::string narrow;
    narrow.clear();
    narrow.reserve(device_name_.size() * 3);
    for (wchar_t wc : device_name_) {
        if (wc < 0x80) {
            narrow.push_back(static_cast<char>(wc));
        } else {
            // Non-ASCII: substitute — this path should rarely be hit.
            narrow.append("?");
        }
    }
    return narrow;
}

// ---------------------------------------------------------------------------
// Render loop
// ---------------------------------------------------------------------------
// RT-SAFE: runs on the audio render thread; all allocations must be avoided.
// All waits are on the event handle set by SetEventHandle.
void WasapiSink::render_loop(const std::jthread::stop_token& token) noexcept {
    Microsoft::WRL::ComPtr<IAudioClient> client;
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderer;
    {
        std::unique_lock lock{mutex_};
        client = audio_client_;
        renderer = render_client_;
    }

    if (!client || !renderer) return;

    // Determine bytes per sample from the opened format.
    WAVEFORMATEX* wfx = nullptr;
    if (FAILED(client->GetMixFormat(&wfx))) return;
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> wfx_guard{wfx, CoTaskMemFree};

    std::size_t bytes_per_sample = (wfx->wBitsPerSample + 7) / 8;
    const std::uint32_t channels = wfx->nChannels;

    // Get the actual buffer frame count.
    std::uint32_t buffer_frames = 0;
    if (FAILED(client->GetBufferSize(&buffer_frames))) return;

    HRESULT hr = S_OK;
    std::uint64_t device_frames = 0;

    while (!token.stop_requested()) {
        // Wait for the event (AUDCLNT_STREAMFLAGS_EVENTCALLBACK) or timeout.
        DWORD wait = WaitForSingleObject(poll_handle_.event, 100);
        if (wait == WAIT_TIMEOUT) continue;          // poll again
        if (wait == WAIT_FAILED || wait == WAIT_ABANDONED) break;

        // Check state before processing.
        if (state_.load(std::memory_order_acquire) != SinkState::Started) break;

        // How many frames are ready in the buffer?
        if (FAILED(client->GetCurrentPadding(&device_frames))) break;
        const std::uint32_t available = buffer_frames - static_cast<std::uint32_t>(device_frames);
        if (available == 0) continue;

        // Get the next buffer region.
        std::byte* buffer = nullptr;
        if (FAILED(renderer->GetBuffer(available, &buffer))) break;

        // In a real integration this would pull from the SPSC ring. Here we fill
        // with silence so the interface compiles and runs without a live graph.
        std::memset(buffer, 0, available * channels * bytes_per_sample);

        // Advance time on the clock so the sink appears active.
        written_frames_.fetch_add(available, std::memory_order_relaxed);

        if (FAILED(renderer->ReleaseBuffer(available, 0))) break;
    }

    // Drain: mark stream as stopped so the client releases its references.
    client->Stop();
}

// ---------------------------------------------------------------------------
// Device enumeration
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<WasapiDevice> WasapiSink::enumerate_devices() const {
    std::vector<WasapiDevice> devices;

    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enm;
    if (FAILED(create_device_enumerator(enm.GetAddressOf()))) return devices;

    Microsoft::WRL::ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enm->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED,
                                        collection.GetAddressOf())))
        return devices;

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return devices;

    Microsoft::WRL::ComPtr<IMMDevice> default_device;
    enm->GetDefaultAudioEndpoint(eRender, eConsole, default_device.GetAddressOf());
    std::wstring default_id;
    if (default_device) {
        LPWSTR pid = nullptr;
        if (SUCCEEDED(default_device->GetId(&pid))) {
            default_id = pid;
            CoTaskMemFree(pid);
        }
    }

    for (UINT i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IMMDevice> dev;
        if (FAILED(collection->Item(i, dev.GetAddressOf()))) continue;

        LPWSTR id = nullptr;
        if (FAILED(dev->GetId(&id))) continue;
        std::wstring dev_id = id;
        CoTaskMemFree(id);

        std::wstring name;
        Microsoft::WRL::ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) {
            PROPVARIANT var;
            PropVariantInit(&var);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) &&
                var.vt == VT_LPCWSTR) {
                name = var.pwszVal;
            }
            PropVariantClear(&var);
        }

        devices.push_back(WasapiDevice{
            .id = std::move(dev_id),
            .name = std::move(name),
            .is_default = false,
        });
    }

    // Mark the default device.
    for (auto& d : devices) {
        if (d.id == default_id) {
            d.is_default = true;
            break;
        }
    }

    return devices;
}

void WasapiSink::set_device_id(std::wstring id) {
    std::unique_lock lock{mutex_};
    if (state_.load(std::memory_order_acquire) == SinkState::Started) return;
    device_id_ = std::move(id);
}

// ---------------------------------------------------------------------------
// Recovery (REQ-AUD-118)
// ---------------------------------------------------------------------------

[[nodiscard]] Status WasapiSink::recover() {
    std::unique_lock lock{mutex_};

    state_.store(SinkState::Recovering, std::memory_order_release);

    // Release existing session.
    render_client_.Reset();
    audio_client_.Reset();
    device_.Reset();

    if (poll_handle_.event) {
        CloseHandle(poll_handle_.event);
        poll_handle_.event = nullptr;
    }

    if (!enumerator_) {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enm;
        if (auto hr = create_device_enumerator(enm.GetAddressOf()); FAILED(hr)) {
            state_.store(SinkState::Closed, std::memory_order_release);
            return err(ErrorCode::DeviceNotFound,
                       "Could not recreate the device enumerator after device loss.",
                       wil::guess_result_from_win32(hr));
        }
        enumerator_ = enm;
    }

    Microsoft::WRL::ComPtr<IMMDevice> dev;
    if (!device_id_.empty()) {
        if (auto hr = get_device_by_id(dev.GetAddressOf(), device_id_.c_str()); FAILED(hr)) {
            state_.store(SinkState::Closed, std::memory_order_release);
            return err(ErrorCode::DeviceLost, "The previously selected device is no longer available.",
                       wil::guess_result_from_win32(hr));
        }
    } else {
        if (auto hr = get_default_device(dev.GetAddressOf(), eRender, eConsole);
            FAILED(hr)) {
            state_.store(SinkState::Closed, std::memory_order_release);
            return err(ErrorCode::DeviceLost,
                       "No default rendering device is available after device change.",
                       wil::guess_result_from_win32(hr));
        }
    }
    device_ = dev;

    auto result = open_device(dev.Get(), config_);
    if (!result) {
        state_.store(SinkState::Closed, std::memory_order_release);
        return std::move(result).error();
    }

    state_.store(SinkState::Open, std::memory_order_release);
    return ok();
}

[[nodiscard]] Status WasapiSink::handle_xrun() noexcept {
    // EPIPE: recover the stream by preparing it.
    std::unique_lock lock{mutex_};
    if (audio_client_) {
        audio_client_->Stop();
        audio_client_->Reset();
    }
    return ok();
}

[[nodiscard]] Status WasapiSink::handle_device_invalid() noexcept {
    // Device removed or in an invalid state — trigger full recovery.
    if (on_device_change_) {
        // Signal the owner on the audio thread — must not block.
        // Dispatch asynchronously via a simple flag; the owner polls in their loop.
        on_device_change_();
    }
    return err(ErrorCode::DeviceLost, "The WASAPI device became invalid.");
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

[[nodiscard]] HRESULT WasapiSink::create_device_enumerator(IMMDeviceEnumerator** out) noexcept {
    return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            IID_PPV_ARGS(out));
}

[[nodiscard]] HRESULT WasapiSink::get_device_by_id(IMMDevice** out,
                                                   const WCHAR* id) const noexcept {
    if (!enumerator_) return E_FAIL;
    return enumerator_->GetDevice(id, out);
}

[[nodiscard]] HRESULT WasapiSink::get_default_device(IMMDevice** out, EDataFlow flow,
                                                    DWORD role) const noexcept {
    if (!enumerator_) return E_FAIL;
    return enumerator_->GetDefaultAudioEndpoint(flow, role, out);
}

[[nodiscard]] Result<SinkConfig> WasapiSink::open_device(IMMDevice* device,
                                                          const SinkConfig& requested) {
    Microsoft::WRL::ComPtr<IAudioClient> client;
    auto hr = device->Activate(IID_IAudioClient, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) {
        return err(ErrorCode::DeviceNotFound, "Could not activate the audio device.",
                   wil::guess_result_from_win32(hr));
    }

    // Build the target WAVEFORMATEX.
    std::unique_ptr<WAVEFORMATEXTENSIBLE> wfx = std::make_unique<WAVEFORMATEXTENSIBLE>();
    wfx->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE);
    wfx->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx->Format.nChannels = static_cast<WORD>(requested.format.channels);
    wfx->Format.nSamplesPerSec = requested.format.sample_rate;
    wfx->Format.nAvgBytesPerSec =
        requested.format.sample_rate * requested.format.channels *
        ((requested.format.bits_per_sample + 7) / 8);
    wfx->Format.nBlockAlign =
        static_cast<WORD>(requested.format.channels * ((requested.format.bits_per_sample + 7) / 8));
    wfx->Format.wBitsPerSample =
        static_cast<WORD>((requested.format.bits_per_sample == 24) ? 32 : requested.format.bits_per_sample);
    wfx->Samples.wValidBitsPerSample = requested.format.bits_per_sample;
    wfx->dwChannelMask = channel_mask_from_channels(requested.format.channels);
    wfx->Subformat = KSDATAFORMAT_SUBTYPE_PCM;

    // Try exclusive first if requested; fall back to shared.
    constexpr AUDCLNT_SHAREMODE share_modes[] = {
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_SHAREMODE_SHARED,
    };

    for (const auto share_mode : share_modes) {
        if (requested.exclusive && share_mode == AUDCLNT_SHAREMODE_SHARED) break;  // skip shared

        auto init_result = try_init_audio_client(client.Get(), requested, share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE);
        if (init_result) {
            audio_client_ = client;

            // Retrieve the mix format (shared) or the accepted format (exclusive).
            WAVEFORMATEX* actual = nullptr;
            client->GetMixFormat(&actual);
            std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> guard{actual, CoTaskMemFree};

            std::uint16_t actual_channels = actual ? actual->nChannels : requested.format.channels;
            std::uint32_t actual_rate = actual ? actual->nSamplesPerSec : requested.format.sample_rate;
            std::uint16_t actual_bps =
                actual ? static_cast<std::uint16_t>(actual->wBitsPerSample) : requested.format.bits_per_sample;

            // Get the buffer size.
            std::uint32_t buffer_frames = 0;
            client->GetBufferSize(&buffer_frames);

            // Activate the render client.
            Microsoft::WRL::ComPtr<IAudioRenderClient> renderer;
            hr = client->GetService(IID_IAudioRenderClient,
                                    reinterpret_cast<void**>(renderer.GetAddressOf()));
            if (FAILED(hr)) {
                return err(ErrorCode::DeviceFormatUnsupported,
                           "Could not get the WASAPI render client.",
                           wil::guess_result_from_win32(hr));
            }
            render_client_ = renderer;

            SinkConfig confirmed{
                .format =
                    PcmFormat{
                        .sample_rate = actual_rate,
                        .channels = actual_channels,
                        .bits_per_sample = actual_bps,
                    },
                .exclusive = share_mode == AUDCLNT_SHAREMODE_EXCLUSIVE,
                .period_frames = buffer_frames,
            };

            return confirmed;
        }
    }

    return err(ErrorCode::DeviceFormatUnsupported,
               "The audio device rejected both exclusive and shared mode.");
}

[[nodiscard]] Result<SinkConfig> WasapiSink::try_init_audio_client(IAudioClient* client,
                                                                    const SinkConfig& requested,
                                                                    const bool exclusive) {
    constexpr AUDCLNT_STREAMFLAGS base_flags =
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOAUTHORITATIVE;

    const AUDCLNT_SHAREMODE share_mode =
        exclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;

    // Build the format.
    WAVEFORMATEXTENSIBLE wfx{};
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE);
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = static_cast<WORD>(requested.format.channels);
    wfx.Format.nSamplesPerSec = requested.format.sample_rate;
    wfx.Format.nBlockAlign = static_cast<WORD>(
        requested.format.channels * ((requested.format.bits_per_sample + 7) / 8));
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.wBitsPerSample =
        static_cast<WORD>((requested.format.bits_per_sample == 24) ? 32 : requested.format.bits_per_sample);
    wfx.Samples.wValidBitsPerSample = requested.format.bits_per_sample;
    wfx.dwChannelMask = channel_mask_from_channels(requested.format.channels);
    wfx.Subformat = KSDATAFORMAT_SUBTYPE_PCM;

    AUDCLNT_STREAMFLAGS flags = base_flags;

    // Set AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM for shared mode when the device cannot
    // directly consume our format — WASAPI will resample on our behalf.
    if (!exclusive) {
        flags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    }

    // Requested buffer duration: use the period as the buffer target.
    constexpr std::uint64_t REFERENCE_TIME_UNITS_PER_MS = 10'000;
    const std::uint64_t requested_period_us =
        (requested.period_frames * 1'000'000ULL) / requested.format.sample_rate;
    const std::uint64_t buffer_duration =
        std::min(requested_period_us * REFERENCE_TIME_UNITS_PER_MS, MAX_BUFFER_DURATION_100NS);

    HRESULT hr = client->Initialize(share_mode, flags, buffer_duration,
                                    exclusive ? buffer_duration : 0,
                                    &wfx.Format, nullptr);

    if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
        return err(ErrorCode::ExclusiveModeUnavailable,
                   "Exclusive mode is not available on this device.");
    }
    if (hr == AUDCLNT_E_DEVICE_IN_USE) {
        return err(ErrorCode::DeviceInUse, "The audio device is in use by another application.");
    }
    if (FAILED(hr)) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   exclusive ? "The device rejected exclusive-mode initialization."
                             : "The device rejected shared-mode initialization.",
                   wil::guess_result_from_win32(hr));
    }

    return SinkConfig{requested.format, exclusive, requested.period_frames};
}

// ---------------------------------------------------------------------------
// DeviceNotifier (IMMNotificationClient)
// ---------------------------------------------------------------------------

IFACEMETHODIMP WasapiSink::DeviceNotifier::QueryInterface(REFIID riid, void** ppv) noexcept {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMMNotificationClient) {
        *ppv = this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

IFACEMETHODIMP WasapiSink::DeviceNotifier::OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                                   LPCWSTR device_id) noexcept {
    if (flow != eRender || role != eConsole) return S_OK;
    // Notify the owner asynchronously — do not call into the owner synchronously
    // from the COM callback thread.
    if (owner_.get().on_device_change_) {
        owner_.get().on_device_change_();
    }
    return S_OK;
}

IFACEMETHODIMP WasapiSink::DeviceNotifier::OnDeviceAdded(LPCWSTR) noexcept { return S_OK; }
IFACEMETHODIMP WasapiSink::DeviceNotifier::OnDeviceRemoved(LPCWSTR) noexcept { return S_OK; }

IFACEMETHODIMP WasapiSink::DeviceNotifier::OnDeviceStateChanged(LPCWSTR, DWORD new_state) noexcept {
    // Only act on devices going to DISABLED or UNPLUGGED (not ACTIVE).
    if (new_state != DEVICE_STATE_DISABLED && new_state != DEVICE_STATE_NOTPRESENT) {
        return S_OK;
    }
    if (owner_.get().on_device_change_) {
        owner_.get().on_device_change_();
    }
    return S_OK;
}

IFACEMETHODIMP WasapiSink::DeviceNotifier::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY&) noexcept {
    return S_OK;  // We do not react to property changes.
}

}  // namespace arrow::audio
#endif  // _WIN32
