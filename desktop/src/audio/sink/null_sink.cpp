// SPDX-License-Identifier: MPL-2.0
#include "audio/sink/null_sink.hpp"

namespace eclipse::audio {

Result<SinkConfig> NullSink::open(const SinkConfig& requested) {
    if (auto valid = requested.format.validate(); !valid) return std::move(valid).error();
    if (requested.period_frames == 0)
        return err(ErrorCode::InvalidArgument, "The sink period is invalid.");
    config_ = requested;
    written_frames_ = 0;
    opened_ = true;
    started_ = false;
    return config_;
}

Status NullSink::start() {
    if (!opened_) return err(ErrorCode::InvalidState, "The sink is not open.");
    started_ = true;
    return ok();
}

void NullSink::stop() noexcept {
    started_ = false;
}

void NullSink::close() noexcept {
    started_ = false;
    opened_ = false;
}

Status NullSink::write(const PlanarFrames frames) noexcept {
    if (!opened_ || !started_) return err(ErrorCode::InvalidState, "The sink is not started.");
    if (!frames.valid() || frames.channels != config_.format.channels) {
        return err(ErrorCode::InvalidArgument, "The sink buffer is invalid.");
    }
    written_frames_ += frames.frames;
    return ok();
}

}  // namespace eclipse::audio
