// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string_view>

#include "audio/ports/audio_ports.hpp"

namespace arrow::audio {

class NullSink final : public IAudioSink {
  public:
    [[nodiscard]] Result<SinkConfig> open(const SinkConfig& requested) override;
    [[nodiscard]] Status start() override;
    void stop() noexcept override;
    void close() noexcept override;
    [[nodiscard]] Status write(PlanarFrames frames) noexcept override;

    [[nodiscard]] std::string_view device_name() const noexcept override { return "null"; }

    [[nodiscard]] std::size_t written_frames() const noexcept { return written_frames_; }

  private:
    SinkConfig config_{};
    std::size_t written_frames_{0};
    bool opened_{false};
    bool started_{false};
};

}  // namespace arrow::audio
