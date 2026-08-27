// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

#include "audio/ports/audio_types.hpp"

namespace eclipse::audio {

struct StreamInfo final {
    PcmFormat format{};
    std::uint64_t total_frames{0};
    std::uint64_t head_trim{0};
    std::uint64_t tail_trim{0};
};

class IDecoder {
  public:
    virtual ~IDecoder() = default;
    [[nodiscard]] virtual Result<StreamInfo> open(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual Result<std::size_t> read(PlanarFrames destination) = 0;
    [[nodiscard]] virtual Status seek(std::uint64_t frame) = 0;
    virtual void close() noexcept = 0;
};

struct SinkConfig final {
    PcmFormat format{};
    bool exclusive{false};
    std::size_t period_frames{0};
};

class IAudioSink {
  public:
    virtual ~IAudioSink() = default;
    [[nodiscard]] virtual Result<SinkConfig> open(const SinkConfig& requested) = 0;
    [[nodiscard]] virtual Status start() = 0;
    virtual void stop() noexcept = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual Status write(PlanarFrames frames) noexcept = 0;
    [[nodiscard]] virtual std::string_view device_name() const noexcept = 0;
};

}  // namespace eclipse::audio
