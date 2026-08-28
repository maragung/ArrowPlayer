// SPDX-License-Identifier: MPL-2.0
#include "audio/graph/playback_graph.hpp"

#include <algorithm>
#include <utility>

namespace arrow::audio {

PlaybackGraph::PlaybackGraph(IDecoder& decoder,
                             IAudioSink& sink,
                             const std::size_t ring_capacity_frames)
      : decoder_{decoder},
        sink_{sink},
        ring_capacity_frames_{ring_capacity_frames},
        ring_{std::make_unique<SpscPcmRing>(
            ring_capacity_frames == 0 ? 1 : ring_capacity_frames, 1)} {}

Status PlaybackGraph::open(const std::filesystem::path& path) {
    if (ring_capacity_frames_ == 0) {
        return err(ErrorCode::InvalidArgument, "The playback buffer size is invalid.");
    }
    auto result = decoder_.open(path);
    if (!result) {
        return std::move(result).error();
    }
    info_ = result.value();
    if (auto valid = info_.format.validate(); !valid) {
        return std::move(valid).error();
    }
    ring_ = std::make_unique<SpscPcmRing>(ring_capacity_frames_, info_.format.channels);
    decode_planes_.assign(info_.format.channels, std::vector<float>(ring_capacity_frames_));
    decode_plane_ptrs_.resize(info_.format.channels);
    consume_planes_.assign(info_.format.channels, std::vector<float>(ring_capacity_frames_));
    consume_plane_ptrs_.resize(info_.format.channels);
    for (std::size_t channel = 0; channel < info_.format.channels; ++channel) {
        decode_plane_ptrs_[channel] = decode_planes_[channel].data();
        consume_plane_ptrs_[channel] = consume_planes_[channel].data();
    }
    const auto sink_result = sink_.open(SinkConfig{info_.format, false, ring_capacity_frames_});
    if (!sink_result) {
        decoder_.close();
        return std::move(sink_result).error();
    }
    if (auto started = sink_.start(); !started) {
        sink_.close();
        decoder_.close();
        return std::move(started).error();
    }
    opened_ = true;
    sink_started_ = true;
    at_end_ = false;
    return ok();
}

Status PlaybackGraph::produce(const std::size_t max_frames) {
    if (!opened_) {
        return err(ErrorCode::InvalidState, "Playback is not open.");
    }
    if (max_frames == 0 || ring_->available_write() == 0) {
        return ok();
    }
    if (at_end_) {
        return ok();
    }
    const auto request =
        std::min(max_frames, std::min(ring_->available_write(), ring_capacity_frames_));
    const auto decoded =
        decoder_.read(PlanarFrames{decode_plane_ptrs_.data(), info_.format.channels, request});
    if (!decoded) {
        return std::move(decoded).error();
    }
    if (decoded.value() == 0) {
        at_end_ = true;
        return ok();
    }
    const auto pushed = ring_->push(
        PlanarFrames{decode_plane_ptrs_.data(), info_.format.channels, decoded.value()});
    if (pushed != decoded.value()) {
        return err(ErrorCode::BufferUnderrun,
                   "The playback buffer could not accept decoded audio.");
    }
    return ok();
}

Status PlaybackGraph::consume(const std::size_t max_frames) noexcept {
    if (!opened_) {
        return err(ErrorCode::InvalidState, "Playback is not open.");
    }
    if (max_frames == 0) {
        return ok();
    }
    const auto count = std::min(max_frames, ring_->available_read());
    if (count == 0) {
        return err(ErrorCode::BufferUnderrun, "The playback buffer has no audio available.");
    }
    const auto popped =
        ring_->pop(PlanarFrames{consume_plane_ptrs_.data(), info_.format.channels, count});
    if (popped != count) {
        return err(ErrorCode::BufferUnderrun, "The playback buffer could not provide audio.");
    }
    return sink_.write(PlanarFrames{consume_plane_ptrs_.data(), info_.format.channels, popped});
}

void PlaybackGraph::close() noexcept {
    if (!opened_) {
        return;
    }
    if (sink_started_) {
        sink_.stop();
        sink_started_ = false;
    }
    sink_.close();
    decoder_.close();
    opened_ = false;
    at_end_ = false;
}

}  // namespace arrow::audio
