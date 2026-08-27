// SPDX-License-Identifier: MPL-2.0
#include "audio/sink/alsa_sink.hpp"

#if defined(ECLIPSE_HAVE_ALSA)
#include <algorithm>
#include <vector>

namespace eclipse::audio {

Result<SinkConfig> AlsaSink::open(const SinkConfig& requested) {
    close();
    if (auto valid = requested.format.validate(); !valid) return std::move(valid).error();
    if (requested.period_frames == 0)
        return err(ErrorCode::InvalidArgument, "The sink period is invalid.");
    if (snd_pcm_open(&handle_, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        return err(ErrorCode::DeviceNotFound, "The ALSA playback device could not be opened.");
    }
    if (snd_pcm_set_params(handle_,
                           SND_PCM_FORMAT_FLOAT_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED,
                           requested.format.channels,
                           requested.format.sample_rate,
                           1,
                           500000) < 0) {
        close();
        return err(ErrorCode::DeviceFormatUnsupported,
                   "The ALSA device rejected the PCM format.");
    }
    config_ = requested;
    opened_ = true;
    return config_;
}

Status AlsaSink::start() {
    if (!opened_ || handle_ == nullptr)
        return err(ErrorCode::InvalidState, "The ALSA sink is not open.");
    return ok();
}

void AlsaSink::stop() noexcept {
    if (handle_ != nullptr) snd_pcm_drop(handle_);
}

void AlsaSink::close() noexcept {
    if (handle_ != nullptr) snd_pcm_close(handle_);
    handle_ = nullptr;
    opened_ = false;
}

Status AlsaSink::write(const PlanarFrames frames) noexcept {
    if (!opened_ || handle_ == nullptr)
        return err(ErrorCode::InvalidState, "The ALSA sink is not open.");
    if (!frames.valid() || frames.channels != config_.format.channels) {
        return err(ErrorCode::InvalidArgument, "The sink buffer is invalid.");
    }
    std::vector<float> interleaved(frames.frames * frames.channels);
    for (std::size_t frame = 0; frame < frames.frames; ++frame)
        for (std::size_t channel = 0; channel < frames.channels; ++channel)
            interleaved[frame * frames.channels + channel] = frames.planes[channel][frame];
    std::size_t written = 0;
    while (written < frames.frames) {
        const auto result = snd_pcm_writei(
            handle_, interleaved.data() + written * frames.channels, frames.frames - written);
        if (result == -EPIPE) {
            snd_pcm_prepare(handle_);
            return err(ErrorCode::BufferUnderrun,
                       "The ALSA playback buffer underrun occurred.");
        }
        if (result < 0)
            return err(ErrorCode::DeviceLost, "The ALSA device stopped accepting audio.");
        written += static_cast<std::size_t>(result);
    }
    return ok();
}

}  // namespace eclipse::audio
#endif
