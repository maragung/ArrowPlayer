// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// PulseAudio async sink implementation.
// RT-SAFE: on_stream_write may be called from the PulseAudio mainloop thread.

#include "audio/sink/pulse_sink.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include "core/error.hpp"

namespace arrow::audio {

namespace {

#if defined(ARROW_HAVE_PULSEAUDIO)

[[nodiscard]] pa_sample_spec make_sample_spec(const PcmFormat& fmt) noexcept {
    pa_sample_spec spec{};
    spec.rate = fmt.sample_rate;
    spec.channels = static_cast<std::uint8_t>(fmt.channels);
    switch (fmt.bits_per_sample) {
        case 16:
            spec.format = PA_SAMPLE_S16LE;
            break;
        case 24:
            spec.format = PA_SAMPLE_S24LE;
            break;
        case 32:
            spec.format = PA_SAMPLE_S32LE;
            break;
        default:
            spec.format = PA_SAMPLE_S16LE;
            break;
    }
    return spec;
}

[[nodiscard]] pa_buffer_attr make_buffer_attr(std::uint32_t latency_us,
                                               const PcmFormat& fmt) noexcept {
    pa_buffer_attr attr{};
    attr.maxlength = static_cast<std::uint32_t>(-1);
    attr.tlength = latency_us * fmt.sample_rate / 1'000'000 *
                   fmt.channels * ((fmt.bits_per_sample + 7) / 8);
    attr.prebuf = attr.tlength;  // same as tlength — no prebuffering
    attr.minreq = attr.tlength / 4;  // request new data when 1/4 of tlength is left
    attr.fragsize = attr.minreq;
    return attr;
}

// Convert float planar frames to interleaved float buffer (PulseAudio native format).
void interleave_planar_float(const PlanarFrames& src, float* dst) noexcept {
    const auto channels = src.channels;
    const auto frames = src.frames;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            dst[f * channels + ch] = std::clamp(src.planes[ch][f], -1.0f, 1.0f);
        }
    }
}

#endif  // ARROW_HAVE_PULSEAUDIO

}  // namespace

// ---------------------------------------------------------------------------
// PulseSink
// ---------------------------------------------------------------------------

PulseSink::PulseSink(PulseDisconnectCallback on_disconnect) noexcept
    : on_disconnect_{std::move(on_disconnect)} {}

PulseSink::~PulseSink() noexcept {
    close();
}

void PulseSink::close() noexcept {
    std::unique_lock lock{mutex_};

#if defined(ARROW_HAVE_PULSEAUDIO)
    started_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);

    if (stream_) {
        pa_stream_set_write_callback(stream_, nullptr, nullptr);
        pa_stream_set_state_callback(stream_, nullptr, nullptr);
        pa_stream_disconnect(stream_);
        pa_stream_unref(stream_);
        stream_ = nullptr;
    }

    if (context_) {
        pa_context_set_subscribe_callback(context_, nullptr, nullptr);
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }

    if (threaded_mainloop_) {
        pa_threaded_mainloop_stop(threaded_mainloop_);
        pa_threaded_mainloop_free(threaded_mainloop_);
        threaded_mainloop_ = nullptr;
    }
    mainloop_ = nullptr;
#endif

    interleaved_buf_.clear();
    config_ = SinkConfig{};
    written_frames_.store(0, std::memory_order_relaxed);
    server_info_.clear();
    device_name_.clear();
}

void PulseSink::set_options(PulseSinkOptions options) noexcept {
    std::unique_lock lock{mutex_};
    if (!started_.load(std::memory_order_acquire)) {
        options_ = std::move(options);
    }
}

[[nodiscard]] Result<SinkConfig> PulseSink::open(const SinkConfig& requested) {
    std::unique_lock lock{mutex_};

    close();

    if (auto valid = requested.format.validate(); !valid) return std::move(valid).error();

#if defined(ARROW_HAVE_PULSEAUDIO)
    // Allocate the threaded mainloop (required for async API).
    threaded_mainloop_ = pa_threaded_mainloop_new();
    if (!threaded_mainloop_) {
        return err(ErrorCode::ResourceExhausted, "Could not create the PulseAudio mainloop.");
    }
    mainloop_ = pa_threaded_mainloop_get_api(threaded_mainloop_);

    // Create the context.
    context_ = pa_context_new(mainloop_,
                               options_.app_name.empty() ? "arrow-player" : options_.app_name.c_str());
    if (!context_) {
        close();
        return err(ErrorCode::ResourceExhausted, "Could not create the PulseAudio context.");
    }

    pa_context_set_state_callback(context_, &PulseSink::context_state_callback, this);
    pa_context_set_subscribe_callback(context_, &PulseSink::subscription_callback, this);

    // Connect to the default server.
    const auto& server = "";  // nullptr means default server
    if (pa_context_connect(context_, server, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        close();
        return err(ErrorCode::DeviceNotFound,
                   "Could not connect to the PulseAudio server.",
                   pa_strerror(pa_context_errno(context_)));
    }

    // The context_state_callback will signal when we are ready.
    pa_threaded_mainloop_start(threaded_mainloop_);

    auto result = wait_for_connection(requested);
    if (!result) {
        close();
        return std::move(result).error();
    }

    config_ = *result;
    return config_;
#else
    return err(ErrorCode::NotImplemented, "PulseAudio is not available in this build.");
#endif
}

[[nodiscard]] Status PulseSink::start() {
#if defined(ARROW_HAVE_PULSEAUDIO)
    std::unique_lock lock{mutex_};
    if (!connected_.load(std::memory_order_acquire)) {
        return err(ErrorCode::InvalidState, "The PulseAudio sink is not connected.");
    }
    started_.store(true, std::memory_order_release);
    return ok();
#else
    return err(ErrorCode::NotImplemented, "PulseAudio is not available in this build.");
#endif
}

void PulseSink::stop() noexcept {
#if defined(ARROW_HAVE_PULSEAUDIO)
    std::unique_lock lock{mutex_};
    if (stream_ && started_.load(std::memory_order_acquire)) {
        // Drain the stream before stopping.
        draining_.store(true, std::memory_order_release);
        pa_stream_drain(stream_, &PulseSink::context_drain_complete, this);
    }
    started_.store(false, std::memory_order_release);
#endif
}

[[nodiscard]] Status PulseSink::write(const PlanarFrames frames) noexcept {
#if defined(ARROW_HAVE_PULSEAUDIO)
    if (!started_.load(std::memory_order_acquire))
        return err(ErrorCode::InvalidState, "The PulseAudio sink is not started.");
    if (!frames.valid())
        return err(ErrorCode::InvalidArgument, "The sink buffer is invalid.");

    if (!interleaved_buf_.empty()) {
        interleave_planar_float(frames, interleaved_buf_.data());

        const std::size_t bytes =
            frames.frames * frames.channels * sizeof(float);

        pa_threaded_mainloop_lock(threaded_mainloop_.load(std::memory_order_acquire));
        if (stream_) {
            const auto result = pa_stream_write(
                stream_,
                interleaved_buf_.data(),
                bytes,
                nullptr,  // no free callback (buffer is owned)
                0,        // offset
                PA_SEEK_RELATIVE);
            if (result < 0) {
                pa_threaded_mainloop_unlock(
                    threaded_mainloop_.load(std::memory_order_acquire));
                return err(ErrorCode::DeviceLost,
                           "Could not write to the PulseAudio stream.",
                           pa_strerror(pa_context_errno(context_)));
            }
            written_frames_.fetch_add(frames.frames, std::memory_order_relaxed);
        }
        pa_threaded_mainloop_unlock(
            threaded_mainloop_.load(std::memory_order_acquire));
    }
    return ok();
#else
    return err(ErrorCode::NotImplemented, "PulseAudio is not available in this build.");
#endif
}

[[nodiscard]] std::string_view PulseSink::device_name() const noexcept {
    std::unique_lock lock{mutex_};
    return device_name_.empty() ? "pulseaudio" : device_name_;
}

// ---------------------------------------------------------------------------
// Connection helpers
// ---------------------------------------------------------------------------

#if defined(ARROW_HAVE_PULSEAUDIO)

[[nodiscard]] Result<SinkConfig> PulseSink::wait_for_connection(const SinkConfig& requested) {
    // Wait for context to reach a ready or failed state.
    for (;;) {
        const auto state = pa_context_get_state(context_);
        if (state == PA_CONTEXT_READY) break;
        if (state == PA_CONTEXT_FAILED) {
            return err(ErrorCode::DeviceNotFound,
                       "The PulseAudio connection failed.",
                       pa_strerror(pa_context_errno(context_)));
        }
        if (state == PA_CONTEXT_TERMINATED) {
            return err(ErrorCode::DeviceLost, "The PulseAudio context was terminated.");
        }
        pa_threaded_mainloop_wait(threaded_mainloop_);
    }

    connected_.store(true, std::memory_order_release);

    // Subscribe to sink events.
    pa_context_subscribe(context_,
                         PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER,
                         nullptr, nullptr);

    // Build the stream.
    return build_stream(requested);
}

[[nodiscard]] Result<SinkConfig> PulseSink::build_stream(const SinkConfig& requested) {
    const auto spec = make_sample_spec(requested.format);
    const auto attr = make_buffer_attr(options_.latency_us, requested.format);

    stream_ = pa_stream_new(context_,
                            options_.stream_name.empty() ? "Music Playback"
                                                        : options_.stream_name.c_str(),
                            &spec, nullptr);
    if (!stream_) {
        return err(ErrorCode::ResourceExhausted, "Could not create the PulseAudio stream.");
    }

    pa_stream_set_write_callback(stream_, &PulseSink::stream_write_callback, this);
    pa_stream_set_state_callback(stream_, &PulseSink::stream_state_callback, this);
    pa_stream_set_latency_update_callback(stream_, &PulseSink::stream_latency_callback, this);

    // Connect to the default sink in shared mode.
    if (pa_stream_connect_playback(stream_, nullptr,  // default sink
                                   &attr,
                                   PA_STREAM_START_CURSOR | PA_STREAM_ADJUST_LATENCY,
                                   nullptr,  // no volume
                                   nullptr) < 0) {
        return err(ErrorCode::DeviceNotFound,
                   "Could not connect the PulseAudio playback stream.",
                   pa_strerror(pa_context_errno(context_)));
    }

    // Wait for the stream to be ready.
    for (;;) {
        const auto state = pa_stream_get_state(stream_);
        if (state == PA_STREAM_READY) break;
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
            return err(ErrorCode::DeviceLost, "The PulseAudio stream failed to initialise.");
        }
        pa_threaded_mainloop_wait(threaded_mainloop_);
    }

    // Fetch the sink name for diagnostics.
    {
        pa_threaded_mainloop_unlock(threaded_mainloop_);
        char* sink_name = nullptr;
        const auto* sink_input_info =
            pa_stream_get_monitor_stream_info(stream_);
        if (sink_input_info && sink_input_info->sink) {
            sink_name = sink_input_info->sink;
        }
        if (sink_name) device_name_ = sink_name;
        pa_threaded_mainloop_lock(threaded_mainloop_);
    }

    // Pre-allocate the interleaved buffer.
    interleaved_buf_.resize(requested.format.channels * requested.period_frames);

    // Retrieve actual format negotiated with the server.
    const auto* actual = pa_stream_get_sample_spec(stream_);

    return SinkConfig{
        PcmFormat{
            actual ? actual->rate : requested.format.sample_rate,
            actual ? actual->channels : requested.format.channels,
            actual ? 32 : requested.format.bits_per_sample,  // PA always 32-bit float internally
        },
        /* exclusive = */ false,
        /* period_frames = */ requested.period_frames,
    };
}

// ---------------------------------------------------------------------------
// PulseAudio callbacks
// ---------------------------------------------------------------------------

void PulseSink::context_state_callback(pa_context* ctx, void* userdata) noexcept {
    auto* self = static_cast<PulseSink*>(userdata);
    const auto state = pa_context_get_state(ctx);

    if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED ||
        state == PA_CONTEXT_TERMINATED) {
        if (self->threaded_mainloop_) {
            pa_threaded_mainloop_signal(self->threaded_mainloop_, 0);
        }
    }
}

void PulseSink::context_drain_complete(pa_context*, int success, void* userdata) noexcept {
    auto* self = static_cast<PulseSink*>(userdata);
    self->draining_.store(false, std::memory_order_release);
}

void PulseSink::stream_write_callback(pa_stream*, std::size_t nbytes,
                                      void* userdata) noexcept {
    // RT-SAFE: called from the PulseAudio async mainloop thread.
    auto* self = static_cast<PulseSink*>(userdata);
    self->on_stream_write(nbytes);
}

void PulseSink::stream_state_callback(pa_stream* stream, void* userdata) noexcept {
    auto* self = static_cast<PulseSink*>(userdata);
    const auto state = pa_stream_get_state(stream);

    if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED) {
        self->connected_.store(false, std::memory_order_release);
        if (self->threaded_mainloop_) {
            pa_threaded_mainloop_signal(self->threaded_mainloop_, 0);
        }
    } else if (state == PA_STREAM_READY) {
        if (self->threaded_mainloop_) {
            pa_threaded_mainloop_signal(self->threaded_mainloop_, 0);
        }
    }
}

void PulseSink::stream_latency_callback(pa_stream*, void*) noexcept {
    // Currently unused; retained for diagnostics in a future iteration.
}

void PulseSink::subscription_callback(pa_context*,
                                     const pa_subscription_event_type_t type,
                                     std::uint32_t,
                                     void* userdata) noexcept {
    auto* self = static_cast<PulseSink*>(userdata);
    const auto facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    if (facility == PA_SUBSCRIPTION_EVENT_SERVER ||
        facility == PA_SUBSCRIPTION_EVENT_SINK) {
        // A default sink may have changed.
        if (self->on_disconnect_) {
            self->on_disconnect_();
        }
    }
}

// RT-SAFE: executes on the PA async mainloop thread.
void PulseSink::on_stream_write(const std::size_t nbytes) noexcept {
    // Note: This is called by PulseAudio when it needs more data.
    // In a full integration, the SPSC ring buffer would be drained here.
    // We do not allocate here (RT-SAFE constraint) so this is a no-op placeholder.
    // The actual audio data is pushed via write() from the playback graph.
}

#endif  // ARROW_HAVE_PULSEAUDIO

}  // namespace arrow::audio
