// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "audio/ports/audio_ports.hpp"

namespace arrow::audio {

class WavDecoder final : public IDecoder {
  public:
    [[nodiscard]] Result<StreamInfo> open(const std::filesystem::path& path) override;
    [[nodiscard]] Result<std::size_t> read(PlanarFrames destination) override;
    [[nodiscard]] Status seek(std::uint64_t frame) override;
    void close() noexcept override;

  private:
    std::ifstream input_;
    StreamInfo info_{};
    std::uint64_t data_offset_{0};
    std::uint64_t data_bytes_{0};
    std::uint64_t position_{0};
    std::uint16_t source_channels_{0};
    std::uint16_t source_bits_{0};
    bool opened_{false};
};

}  // namespace arrow::audio
