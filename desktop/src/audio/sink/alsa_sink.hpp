// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// ALSA sink — REQ-AUD-067 pipe/shim detection, REQ-AUD-121 explicit period/buffer
// sizing, EPIPE/XRUN recovery per REQ-AUD-118.  Event-driven via snd_pcm_poll_descriptors.
// Supports both shared mode (plughw:) and direct/bit-perfect mode (hw:).

#include "audio/ports/i_audio_sink.hpp"
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "audio/ports/audio_ports.hpp"

#if defined(ARROW_HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

namespace arrow::audio {

/// Callback invoked when a device is lost (disconnect, hot-unplug).
/// RT-SAFE: called from the ALSA poll thread — must not block or allocate.
using AlsaDeviceChangeCallback = std::function<void()>;

/// ALSA sink configuration hints.
struct AlsaSinkOptions final {
    /// ALSA device name, e.g. "default", "plughw:0", "hw:0".
    /// Empty means "default".
    std::string device{"default"};
    /// If true, use "plughw:" prefix (shared/intermediate format path).
    /// If false, use "hw:" prefix (direct/hardware path, bit-perfect).
    bool use_plug{true};
    /// Target buffer duration in microseconds.  The sink will derive a
    /// period/buffer configuration from this and the requested period_frames.
    std::uint32_t target_buffer_us{50'000};  // 50 ms default
    /// Target period size in frames.  Zero means auto-select.
    std::uint32_t period_frames{0};
};

/// Information about the currently opened device.
struct AlsaDeviceInfo final {
    /// ALSA card/device identifier, e.g. "hw:0".
    std::string device_id;
    /// Human-readable name.
    std::string name;
    /// True if the device is a PipeWire or PulseAudio compatibility shim
    /// (detected by probing the card name for "pipewire", "pulse", "pci" hints).
    bool is_shim{false};
    /// True if the device was opened in "hw:" direct mode (bit-perfect capable).
    bool direct_mode{false};
};

/// ALSA audio sink.
///
/// Implements IAudioSink against libasound.  Uses snd_pcm_hw_params for
/// explicit period/buffer sizing and event-driven operation via poll.
class AlsaSink final : public IAudioSink {
  public:
    /// Constructs a sink.  Pass a device-change callback to receive notifications
    /// when the hardware device is lost.
    explicit AlsaSink(AlsaDeviceChangeCallback on_device_change = nullptr) noexcept;
    ~AlsaSink() noexcept override;

    AlsaSink(const AlsaSink&) = delete;
    AlsaSink& operator=(const AlsaSink&) = delete;
    AlsaSink(AlsaSink&&) = delete;
    AlsaSink& operator=(AlsaSink&&) = delete;

    // -----------------------------------------------------------------------
    // IAudioSink interface
    // -----------------------------------------------------------------------
    [[nodiscard]] Result<SinkConfig> open(const SinkConfig& requested) override;
    [[nodiscard]] Status start() override;
    void stop() noexcept override;
    void close() noexcept override;
    [[nodiscard]] Status write(PlanarFrames frames) noexcept override;
    [[nodiscard]] std::string_view device_name() const noexcept override;

    // -----------------------------------------------------------------------
    // ALSA-specific API
    // -----------------------------------------------------------------------

    /// Returns the resolved device name (may differ from requested if plughw: was used).
    [[nodiscard]] const AlsaDeviceInfo& device_info() const noexcept { return info_; }

    /// Returns the number of frames successfully written.
    [[nodiscard]] std::size_t written_frames() const noexcept {
        return written_frames_.load(std::memory_order_relaxed);
    }

    /// Re-opens the device using the same options as the initial open.
    /// Used by the recovery state machine (REQ-AUD-118).
    [[nodiscard]] Status recover();

    /// Sets configuration options before the next open() call.
    void set_options(AlsaSinkOptions options) noexcept;

  private:
    // Resolves the effective device name (adds plughw:/hw: prefix).
    [[nodiscard]] std::string resolve_device_name() const;

    // Probes whether the card behind the given pcm handle is a shim.
    [[nodiscard]] bool detect_shim() const;

    // Applies snd_pcm_hw_params with explicit period/buffer sizing.
    [[nodiscard]] Result<SinkConfig> configure_hw_params(snd_pcm_t* handle,
                                                          const SinkConfig& requested);

    // Converts a native ALSA error to an arrow error.
    [[nodiscard]] Error alsa_error(int errcode, std::string context) const;

    // RT callback: the poll thread wakes on underrun/overrun events.
    void poll_loop() noexcept;

    // Internal state.
    SinkConfig config_{};
    AlsaDeviceInfo info_{};
    AlsaSinkOptions options_{};

    mutable std::mutex mutex_;

#if defined(ARROW_HAVE_ALSA)
    snd_pcm_t* handle_{nullptr};
    snd_pcm_hw_params_t* hw_params_{nullptr};  // reusable param block
#endif

    std::atomic<bool> started_{false};
    std::atomic<bool> xrun_detected_{false};
    std::atomic<std::size_t> written_frames_{0};
    std::jthread poll_thread_;
    std::vector<struct pollfd> poll_fds_;
    std::vector<std::byte> interleaved_buf_;

    AlsaDeviceChangeCallback on_device_change_;
};

}  // namespace arrow::audio
