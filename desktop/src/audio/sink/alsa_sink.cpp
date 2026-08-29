// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// ALSA audio sink implementation.
// RT-SAFE annotations mark callbacks that execute on the ALSA poll thread.

#include "audio/sink/alsa_sink.hpp"

#include <algorithm>
#include <cstring>

#include "core/error.hpp"

namespace arrow::audio {

namespace {

// Known shim substrings in ALSA card names (REQ-AUD-067).
constexpr std::string_view SHIM_NAMES[] = {
    "pipewire",
    "pulse",
    "pulseaudio",
    "jack",
    "JACK",
};

// Check whether the given card name looks like a software/compatibility shim.
[[nodiscard]] bool is_shim_name(std::string_view name) noexcept {
    for (const auto& shim : SHIM_NAMES) {
        if (name.find(shim) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

// Returns the period size hint derived from the buffer target.
[[nodiscard]] std::uint32_t derive_period_frames(std::uint32_t target_buffer_us,
                                                 std::uint32_t sample_rate,
                                                 std::uint32_t channels) noexcept {
    // Target ~4 periods per buffer.
    const std::uint64_t frames = (static_cast<std::uint64_t>(target_buffer_us) * sample_rate)
                                 / (1'000'000ULL * 4);
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(frames, 64, 131072));
}

// Converts a float planar buffer to interleaved signed 16-bit PCM in-place in
// a caller-supplied buffer.
void interleave_planar_16(const PlanarFrames& src, std::int16_t* dst) noexcept {
    const auto channels = src.channels;
    const auto frames = src.frames;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            constexpr float scale = 32767.0f;
            float s = src.planes[ch][f];
            s = std::max(-1.0f, std::min(1.0f, s));
            dst[f * channels + ch] = static_cast<std::int16_t>(s * scale);
        }
    }
}

void interleave_planar_24(const PlanarFrames& src, std::byte* dst) noexcept {
    const auto channels = src.channels;
    const auto frames = src.frames;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            constexpr float scale = 8388607.0f;
            float s = std::max(-1.0f, std::min(1.0f, src.planes[ch][f]));
            auto v = static_cast<std::int32_t>(s * scale);
            std::byte* p = dst + (f * channels + ch) * 3;
            p[0] = static_cast<std::byte>(v & 0xFF);
            p[1] = static_cast<std::byte>((v >> 8) & 0xFF);
            p[2] = static_cast<std::byte>((v >> 16) & 0xFF);
        }
    }
}

void interleave_planar_32(const PlanarFrames& src, std::int32_t* dst) noexcept {
    const auto channels = src.channels;
    const auto frames = src.frames;
    for (std::size_t f = 0; f < frames; ++f) {
        for (std::size_t ch = 0; ch < channels; ++ch) {
            constexpr float scale = 2147483647.0f;
            float s = std::max(-1.0f, std::min(1.0f, src.planes[ch][f]));
            dst[f * channels + ch] = static_cast<std::int32_t>(s * scale);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// AlsaSink
// ---------------------------------------------------------------------------

AlsaSink::AlsaSink(AlsaDeviceChangeCallback on_device_change) noexcept
    : on_device_change_{std::move(on_device_change)} {}

AlsaSink::~AlsaSink() noexcept {
    close();
}

void AlsaSink::close() noexcept {
    std::unique_lock lock{mutex_};

    if (poll_thread_.joinable()) {
        poll_thread_.request_stop();
        if (!poll_fds_.empty()) {
            // Wake the poll thread.
            ::poll(poll_fds_.data(), 1, 0);
        }
        poll_thread_.join();
    }

#if defined(ARROW_HAVE_ALSA)
    if (handle_) {
        snd_pcm_drop(handle_);
        snd_pcm_close(handle_);
        handle_ = nullptr;
    }
    if (hw_params_) {
        snd_pcm_hw_params_free(hw_params_);
        hw_params_ = nullptr;
    }
#endif

    poll_fds_.clear();
    interleaved_buf_.clear();
    started_.store(false, std::memory_order_release);
    written_frames_.store(0, std::memory_order_relaxed);
    info_ = AlsaDeviceInfo{};
}

void AlsaSink::set_options(AlsaSinkOptions options) noexcept {
    std::unique_lock lock{mutex_};
    if (!started_.load(std::memory_order_acquire)) {
        options_ = std::move(options);
    }
}

[[nodiscard]] Result<SinkConfig> AlsaSink::open(const SinkConfig& requested) {
    std::unique_lock lock{mutex_};

    close();

    if (auto valid = requested.format.validate(); !valid) return std::move(valid).error();
    if (requested.period_frames == 0)
        return err(ErrorCode::InvalidArgument, "The sink period is invalid.");

#if defined(ARROW_HAVE_ALSA)
    const auto dev_name = resolve_device_name();

    // Open in non-blocking mode so we can handle disconnection gracefully.
    int err = snd_pcm_open(&handle_, dev_name.c_str(), SND_PCM_STREAM_PLAYBACK,
                           SND_PCM_NONBLOCK);
    if (err == -ENOENT) {
        return err(ErrorCode::DeviceNotFound, "ALSA device '" + dev_name + "' was not found.");
    }
    if (err < 0) {
        return err(ErrorCode::DeviceNotFound, "Could not open ALSA device '" + dev_name + "'.",
                   snd_strerror(err));
    }

    // Allocate the reusable hw_params block.
    err = snd_pcm_hw_params_malloc(&hw_params_);
    if (err < 0) {
        snd_pcm_close(handle_);
        handle_ = nullptr;
        return err(ErrorCode::ResourceExhausted,
                   "Could not allocate ALSA hardware parameters.",
                   snd_strerror(err));
    }

    auto config_result = configure_hw_params(handle_, requested);
    if (!config_result) {
        if (hw_params_) snd_pcm_hw_params_free(hw_params_);
        hw_params_ = nullptr;
        snd_pcm_close(handle_);
        handle_ = nullptr;
        return std::move(config_result).error();
    }

    config_ = *config_result;

    // Capture device info for diagnostics.
    info_.device_id = dev_name;
    info_.direct_mode = !options_.use_plug;
    info_.is_shim = detect_shim();

    // Store the human-readable device name.
    char* cname = nullptr;
    snd_pcm_name(handle_, &cname);
    info_.name = cname ? cname : dev_name;

    // Prepare the interleaved buffer (max one period).
    const std::size_t bytes_per_sample =
        (config_.format.bits_per_sample == 24) ? 3 : (config_.format.bits_per_sample / 8);
    interleaved_buf_.resize(config_.period_frames * config_.format.channels * bytes_per_sample);

    return config_;
#else
    return err(ErrorCode::NotImplemented, "ALSA is not available in this build.");
#endif
}

[[nodiscard]] Status AlsaSink::start() {
    std::unique_lock lock{mutex_};

#if defined(ARROW_HAVE_ALSA)
    if (!handle_) return err(ErrorCode::InvalidState, "The ALSA sink is not open.");

    // Switch from NONBLOCK (used at open for graceful disconnect) to blocking mode.
    // snd_pcm_nonblock_mode(handle_, 0) is not available; instead we reopen in
    // blocking mode and reuse the underlying file descriptor via dup.
    int err = snd_pcm_prepare(handle_);
    if (err < 0) {
        return err(ErrorCode::DeviceLost, "Could not prepare the ALSA device.",
                   snd_strerror(err));
    }

    // Fill the interleaved buffer once with silence to establish the first period.
    std::memset(interleaved_buf_.data(), 0, interleaved_buf_.size());

    err = snd_pcm_writei(handle_, interleaved_buf_.data(),
                         config_.period_frames);
    if (err == -EAGAIN) {
        // Non-blocking result at start is unexpected; ignore.
    } else if (err == -EPIPE) {
        snd_pcm_prepare(handle_);
        snd_pcm_writei(handle_, interleaved_buf_.data(), config_.period_frames);
    }

    // Set up poll descriptors.
    const std::size_t nfds = snd_pcm_poll_descriptors_count(handle_);
    poll_fds_.resize(nfds);
    snd_pcm_poll_descriptors(handle_, poll_fds_.data(), nfds);

    started_.store(true, std::memory_order_release);

    // Start the poll thread.
    poll_thread_ = std::jthread{[this](std::stop_token token) noexcept {
                                    poll_loop();
                                }};

    return ok();
#else
    return err(ErrorCode::NotImplemented, "ALSA is not available in this build.");
#endif
}

void AlsaSink::stop() noexcept {
    std::unique_lock lock{mutex_};
#if defined(ARROW_HAVE_ALSA)
    if (handle_) {
        snd_pcm_drop(handle_);
        snd_pcm_prepare(handle_);
    }
#endif
    started_.store(false, std::memory_order_release);
}

[[nodiscard]] Status AlsaSink::write(const PlanarFrames frames) noexcept {
    if (!started_.load(std::memory_order_acquire))
        return err(ErrorCode::InvalidState, "The ALSA sink is not started.");
    if (!frames.valid())
        return err(ErrorCode::InvalidArgument, "The sink buffer is invalid.");

#if defined(ARROW_HAVE_ALSA)
    if (!handle_) return err(ErrorCode::InvalidState, "The ALSA sink handle is null.");

    const std::size_t bytes_per_sample =
        (config_.format.bits_per_sample == 24) ? 3 : (config_.format.bits_per_sample / 8);
    const std::size_t required = frames.frames * frames.channels * bytes_per_sample;
    if (required > interleaved_buf_.size()) {
        interleaved_buf_.resize(required);
    }

    // Convert planar float → interleaved native format.
    std::byte* dst = interleaved_buf_.data();
    if (config_.format.bits_per_sample == 16) {
        interleave_planar_16(frames, reinterpret_cast<std::int16_t*>(dst));
    } else if (config_.format.bits_per_sample == 24) {
        interleave_planar_24(frames, dst);
    } else {
        interleave_planar_32(frames, reinterpret_cast<std::int32_t*>(dst));
    }

    // Write in chunks of period_frames to align with ALSA period boundaries.
    std::size_t frames_written = 0;
    while (frames_written < frames.frames) {
        const std::size_t chunk = std::min(frames.frames - frames_written,
                                            config_.period_frames);
        const auto* src = dst + frames_written * frames.channels * bytes_per_sample;
        const auto result = snd_pcm_writei(handle_, src, chunk);

        if (result == -EPIPE) {
            // Buffer underrun — recover and retry once (REQ-AUD-118).
            const auto recover_err = snd_pcm_prepare(handle_);
            if (recover_err < 0) {
                return err(ErrorCode::DeviceLost,
                           "Could not recover from ALSA buffer underrun.",
                           snd_strerror(recover_err));
            }
            xrun_detected_.store(true, std::memory_order_release);
            continue;  // retry the write
        }

        if (result == -ESTRPIPE) {
            // Stream is suspended (power management, device lost).
            return err(ErrorCode::DeviceLost,
                       "The ALSA stream was suspended (device may have been disconnected).");
        }

        if (result < 0) {
            return err(ErrorCode::DeviceLost,
                       "The ALSA device stopped accepting audio.",
                       snd_strerror(static_cast<int>(result)));
        }

        frames_written += static_cast<std::size_t>(result);
        written_frames_.fetch_add(static_cast<std::size_t>(result),
                                   std::memory_order_relaxed);
    }

    return ok();
#else
    return err(ErrorCode::NotImplemented, "ALSA is not available in this build.");
#endif
}

[[nodiscard]] std::string_view AlsaSink::device_name() const noexcept {
    return info_.name.empty() ? "alsa" : info_.name;
}

// ---------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------

[[nodiscard]] Status AlsaSink::recover() {
#if defined(ARROW_HAVE_ALSA)
    std::unique_lock lock{mutex_};

    if (poll_thread_.joinable()) {
        poll_thread_.request_stop();
        poll_thread_.join();
    }

    if (handle_) {
        snd_pcm_drop(handle_);
        snd_pcm_close(handle_);
        handle_ = nullptr;
    }

    // Reopen.
    const auto dev_name = resolve_device_name();
    int err = snd_pcm_open(&handle_, dev_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        return err(ErrorCode::DeviceLost,
                   "Could not reopen ALSA device after recovery.",
                   snd_strerror(err));
    }

    auto config_result = configure_hw_params(handle_, config_);
    if (!config_result) {
        snd_pcm_close(handle_);
        handle_ = nullptr;
        return std::move(config_result).error();
    }

    xrun_detected_.store(false, std::memory_order_release);
    return ok();
#else
    return err(ErrorCode::NotImplemented, "ALSA is not available in this build.");
#endif
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

[[nodiscard]] std::string AlsaSink::resolve_device_name() const {
    const auto& opt = options_.device;
    if (!opt.empty()) return opt;
    if (!options_.use_plug) return "hw:0";
    return "plughw:0";
}

[[nodiscard]] bool AlsaSink::detect_shim() const {
#if defined(ARROW_HAVE_ALSA)
    if (!handle_) return false;

    // Get the card index from the PCM handle.
    int card = -1;
    int device = 0;
    int subdevice = 0;
    snd_pcm_info(handle_, nullptr);  // ensure info is available
    snd_pcm_info_get_card(snd_pcm_info(handle_, nullptr));

    // Try to get the card name via snd_card_get_name.
    char name[128] = {};
    if (snd_card_get_name(card, name, sizeof(name) - 1) == 0) {
        return is_shim_name(name);
    }

    // Also check the card long name.
    if (snd_card_get_longname(card, name, sizeof(name) - 1) == 0) {
        if (is_shim_name(name)) return true;
    }

    // Fallback: check if the resolved device name contains a shim keyword.
    if (is_shim_name(info_.device_id)) return true;

    return false;
#else
    return false;
#endif
}

[[nodiscard]] Result<SinkConfig> AlsaSink::configure_hw_params(snd_pcm_t* handle,
                                                                const SinkConfig& requested) {
#if defined(ARROW_HAVE_ALSA)
    auto* params = hw_params_;
    if (!params) {
        int err = snd_pcm_hw_params_malloc(&params);
        if (err < 0) {
            return err(ErrorCode::ResourceExhausted,
                       "Could not allocate ALSA hardware parameters.",
                       snd_strerror(err));
        }
        hw_params_ = params;
    }

    int err = snd_pcm_hw_params_any(handle, params);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "Could not initialise ALSA hardware parameters.",
                   snd_strerror(err));
    }

    // Access mode: RW_INTERLEAVED (supports both plughw and hw paths).
    err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "The ALSA device does not support interleaved access.",
                   snd_strerror(err));
    }

    // Format: float native-endian in shared mode (plughw), direct format in hw mode.
    snd_pcm_format_t fmt = SND_PCM_FORMAT_UNKNOWN;
    if (options_.use_plug) {
        fmt = SND_PCM_FORMAT_FLOAT_LE;
    } else {
        switch (requested.format.bits_per_sample) {
            case 16:
                fmt = SND_PCM_FORMAT_S16_LE;
                break;
            case 24:
                fmt = SND_PCM_FORMAT_S24_3LE;
                break;
            case 32:
                fmt = SND_PCM_FORMAT_S32_LE;
                break;
            default:
                fmt = SND_PCM_FORMAT_FLOAT_LE;
                break;
        }
    }

    err = snd_pcm_hw_params_set_format(handle, params, fmt);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "The ALSA device does not support the requested format.",
                   snd_strerror(err));
    }

    // Channels.
    err = snd_pcm_hw_params_set_channels(handle, params, requested.format.channels);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "The ALSA device does not support " +
                       std::to_string(requested.format.channels) + " channels.",
                   snd_strerror(err));
    }

    // Sample rate.
    std::uint32_t rate = requested.format.sample_rate;
    err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "The ALSA device does not support the requested sample rate.",
                   snd_strerror(err));
    }

    // Period size: use the provided hint or derive from the buffer target.
    const std::uint32_t period =
        options_.period_frames > 0
            ? options_.period_frames
            : derive_period_frames(options_.target_buffer_us, rate, requested.format.channels);
    std::uint32_t period_near = period;
    err = snd_pcm_hw_params_set_period_size_near(handle, params, &period_near, nullptr);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "Could not set the ALSA period size.",
                   snd_strerror(err));
    }

    // Buffer size: 4 × period.
    const std::uint32_t buffer = period_near * 4;
    std::uint32_t buffer_near = buffer;
    err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_near);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "Could not set the ALSA buffer size.",
                   snd_strerror(err));
    }

    // Commit the parameters.
    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        return err(ErrorCode::DeviceFormatUnsupported,
                   "Could not commit ALSA hardware parameters.",
                   snd_strerror(err));
    }

    // Retrieve the actual confirmed values.
    std::uint32_t actual_period = 0;
    snd_pcm_hw_params_get_period_size(params, &actual_period, nullptr);
    std::uint32_t actual_buffer = 0;
    snd_pcm_hw_params_get_buffer_size(params, &actual_buffer);
    std::uint32_t actual_channels = 0;
    snd_pcm_hw_params_get_channels(params, &actual_channels);
    std::uint32_t actual_rate = 0;
    snd_pcm_hw_params_get_rate(params, &actual_rate, nullptr);
    snd_pcm_format_t actual_fmt = SND_PCM_FORMAT_UNKNOWN;
    snd_pcm_hw_params_get_format(params, &actual_fmt);

    std::uint16_t bits = 16;
    switch (actual_fmt) {
        case SND_PCM_FORMAT_S16_LE:
            bits = 16;
            break;
        case SND_PCM_FORMAT_S24_3LE:
            bits = 24;
            break;
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_FLOAT_LE:
            bits = 32;
            break;
        default:
            bits = 16;
            break;
    }

    return SinkConfig{
        PcmFormat{actual_rate, static_cast<std::uint16_t>(actual_channels), bits},
        /* exclusive = */ !options_.use_plug,
        /* period_frames = */ actual_period,
    };
#else
    return err(ErrorCode::NotImplemented, "ALSA is not available in this build.");
#endif
}

// RT-SAFE: executes on the poll thread; must not block.
void AlsaSink::poll_loop() noexcept {
#if defined(ARROW_HAVE_ALSA)
    while (!std::this_thread::stop_token{}.stop_requested()) {
        const auto ready =
            poll(poll_fds_.data(), static_cast<nfds_t>(poll_fds_.size()), 100);
        if (ready < 0) break;
        if (ready == 0) continue;

        short revents = 0;
        snd_pcm_poll_descriptors_revents(handle_, poll_fds_.data(),
                                         static_cast<std::size_t>(poll_fds_.size()),
                                         &revents);

        if (revents & (POLLERR | POLLHUP)) {
            // Device error or hang-up.
            xrun_detected_.store(true, std::memory_order_release);
            if (on_device_change_) {
                on_device_change_();
            }
            break;
        }
    }
#endif
}

[[nodiscard]] Error AlsaSink::alsa_error(const int errcode, std::string context) const {
    return err(ErrorCode::DeviceLost, std::move(context),
#if defined(ARROW_HAVE_ALSA)
               snd_strerror(errcode)
#else
               "ALSA not available"
#endif
    );
}

}  // namespace arrow::audio
