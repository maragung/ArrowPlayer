// SPDX-License-Identifier: MPL-2.0
#include "audio/playback_service.hpp"

namespace eclipse::audio {

Status PlaybackService::play_file(const std::filesystem::path& path,
                                  const std::size_t chunk_frames) {
    if (chunk_frames == 0)
        return err(ErrorCode::InvalidArgument, "The playback chunk size is invalid.");
    if (auto opened = graph_.open(path); !opened) return opened;
    while (!graph_.at_end() || graph_.queued_frames() > 0) {
        if (!graph_.at_end()) {
            if (auto produced = graph_.produce(chunk_frames); !produced) {
                graph_.close();
                return produced;
            }
        }
        while (graph_.queued_frames() > 0) {
            if (auto consumed = graph_.consume(chunk_frames); !consumed) {
                graph_.close();
                return consumed;
            }
        }
    }
    graph_.close();
    return ok();
}

}  // namespace eclipse::audio
