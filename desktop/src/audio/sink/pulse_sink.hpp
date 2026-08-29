// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// PulseAudio async sink — shared mode only, non-bit-perfect (REQ-AUD-067 label).
// Handles server disconnect and default-sink changes via a pulse subscription mask.

#pragma once
#include "audio/ports/i_audio_sink.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "audio/ports/audio_ports.hpp"

#if defined(ARROW_HAVE_PULSEAUDIO)
#include <pulse/pulseaudio.h>
#endif

namespace arrow::audio {

/// Callback invoked when the PulseAudio server disconnects or a device change occurs.
using PulseDisconnectCallback = std::function<void()>;

/// PulseAudio sink options.
struct PulseSinkOptions final {
    /// Application name exposed to PulseAudio, e.g. "arrow-player".
    std::string app_name{"arrow-player"};
    /// Stream name, e.g. "Music Playback".
    std::string stream_name{"Music Playback"};
    /// Desired latency in microseconds (used as the stream latency hint).
    std::uint32_t latency_us{50'000};
};

/// PulseAudio audio sink.
///
/// Implements IAudioSink using the PulseAudio async API.
///
// Unlike ALSA and WASAPI, PulseAudio is always shared-mode and never bit-perfect.
/// The sink is labelled as non-bit-perfect in diagnostics.
class PulseSink final : public IAudioSink {
  public:
    explicit PulseSink(PulseDisconnectCallback on_disconnect = nullptr) noexcept;
    ~PulseSink() noexcept override;

    PulseSink(const PulseSink&) = delete;
    PulseSink& operator=(const PulseSink&) = delete;
    PulseSink(PulseSink&&) = delete;
    PulseSink& operator=(PulseSink&&) = delete;

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
    // PulseAudio-specific API
    // -----------------------------------------------------------------------

    /// Returns true if the PulseAudio connection is currently connected.
    [[nodiscard]] bool is_connected() const noexcept {
        return connected_.load(std::memory_order_acquire);
    }

    /// The PulseAudio server info string (null until connected).
    [[nodiscard]] const std::string& server_info() const noexcept {
        return server_info_;
    }

    /// Sets options before the next open() call.
    void set_options(PulseSinkOptions options) noexcept;

  private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    static void context_state_callback(pa_context*, void* userdata) noexcept;
    static void context_drain_complete(pa_context*, int success, void* userdata) noexcept;
    static void stream_write_callback(pa_stream*, std::size_t nbytes, void* userdata) noexcept;
    static void stream_state_callback(pa_stream*, void* userdata) noexcept;
    static void stream_latency_callback(pa_stream*, void* userdata) noexcept;
    static void subscription_callback(pa_context*, pa_subscription_event_type_t type,
                                     std::uint32_t idx, void* userdata) noexcept;

    // RT callback: writes interleaved data to the PulseAudio stream.
    // RT-SAFE: called by the PulseAudio async mainloop.
    void on_stream_write(std::size_t nbytes) noexcept;

    [[nodiscard]] Result<SinkConfig> wait_for_connection(const SinkConfig& requested);
    [[nodiscard]] Result<SinkConfig> build_stream(const SinkConfig& requested);

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    mutable std::mutex mutex_;

    PulseSinkOptions options_{};
    SinkConfig config_{};

    std::atomic<bool> connected_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> draining_{false};
    std::atomic<std::size_t> written_frames_{0};

    std::string server_info_;
    std::string device_name_;

    std::vector<float> interleaved_buf_;

#if defined(ARROW_HAVE_PULSEAUDIO)
    pa_mainloop* mainloop_{nullptr};
    pa_context* context_{nullptr};
    pa_stream* stream_{nullptr};
    pa_threaded_mainloop* threaded_mainloop_{nullptr};
#endif

    PulseDisconnectCallback on_disconnect_;
};

}  // namespace arrow::audio
