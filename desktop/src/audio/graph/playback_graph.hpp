// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
#pragma once

#include <cstddef>
#include <vector>

#include "audio/graph/spsc_ring.hpp"
#include "audio/ports/audio_ports.hpp"

namespace eclipse::audio {

/// Coordinates non-RT decoder production and bounded sink consumption.
/// Call produce() from a worker thread and consume() from the sink callback.
class PlaybackGraph final {
  public:
    PlaybackGraph(IDecoder& decoder, IAudioSink& sink, std::size_t ring_capacity_frames);

    [[nodiscard]] Status open(const std::filesystem::path& path);
    [[nodiscard]] Status produce(std::size_t max_frames);
    [[nodiscard]] Status consume(std::size_t max_frames) noexcept;
    void close() noexcept;

    [[nodiscard]] std::size_t queued_frames() const noexcept { return ring_ == nullptr ? 0 : ring_->available_read(); }
    [[nodiscard]] bool at_end() const noexcept { return at_end_; }
    [[nodiscard]] const StreamInfo& stream_info() const noexcept { return info_; }

  private:
    IDecoder& decoder_;
    IAudioSink& sink_;
    const std::size_t ring_capacity_frames_;
    std::unique_ptr<SpscPcmRing> ring_;
    StreamInfo info_{};
    std::vector<std::vector<float>> decode_planes_;
    std::vector<float*> decode_plane_ptrs_;
    std::vector<std::vector<float>> consume_planes_;
    std::vector<float*> consume_plane_ptrs_;
    bool opened_{false};
    bool sink_started_{false};
    bool at_end_{false};
};

}  // namespace eclipse::audio
