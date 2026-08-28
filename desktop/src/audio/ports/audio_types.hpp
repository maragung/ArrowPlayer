// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/error.hpp"

namespace arrow::audio {

/// Interleaved/planar-independent PCM stream description.
struct PcmFormat final {
    std::uint32_t sample_rate{0};
    std::uint16_t channels{0};
    std::uint16_t bits_per_sample{0};

    [[nodiscard]] Status validate() const noexcept {
        if (sample_rate == 0 || sample_rate > 768000) {
            return err(ErrorCode::InvalidArgument, "The sample rate is invalid.");
        }
        if (channels == 0 || channels > 32) {
            return err(ErrorCode::InvalidArgument, "The channel count is invalid.");
        }
        if (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32) {
            return err(ErrorCode::InvalidArgument, "The PCM bit depth is unsupported.");
        }
        return ok();
    }

    friend bool operator==(const PcmFormat&, const PcmFormat&) = default;
};

/// Non-owning planar frame block. The producer owns the memory for the call.
struct PlanarFrames final {
    float* const* planes{nullptr};
    std::size_t channels{0};
    std::size_t frames{0};

    [[nodiscard]] bool valid() const noexcept {
        if (frames == 0 || channels == 0 || planes == nullptr) {
            return frames == 0;
        }
        for (std::size_t channel = 0; channel < channels; ++channel) {
            if (planes[channel] == nullptr) {
                return false;
            }
        }
        return true;
    }
};

}  // namespace arrow::audio
