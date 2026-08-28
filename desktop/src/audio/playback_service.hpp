// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <filesystem>

#include "audio/graph/playback_graph.hpp"

namespace arrow::audio {

class PlaybackService final {
  public:
    PlaybackService(IDecoder& decoder, IAudioSink& sink, std::size_t buffer_frames)
          : graph_{decoder, sink, buffer_frames} {}

    [[nodiscard]] Status play_file(const std::filesystem::path& path, std::size_t chunk_frames);

    void stop() noexcept { graph_.close(); }

  private:
    PlaybackGraph graph_;
};

}  // namespace arrow::audio
