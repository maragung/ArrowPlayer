// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "audio/ports/audio_types.hpp"

namespace eclipse::audio {

/// Preallocated SPSC ring of planar float frames.
/// One producer and one consumer only; capacity is fixed after construction.
class SpscPcmRing final {
  public:
    SpscPcmRing(std::size_t capacity_frames, std::size_t channels)
          : capacity_{capacity_frames},
            channels_{channels},
            samples_(capacity_frames * channels, 0.0F) {}

    SpscPcmRing(const SpscPcmRing&) = delete;
    SpscPcmRing& operator=(const SpscPcmRing&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] std::size_t available_read() const noexcept {
        const auto write = write_index_.load(std::memory_order_acquire);
        const auto read = read_index_.load(std::memory_order_relaxed);
        return write - read;
    }

    [[nodiscard]] std::size_t available_write() const noexcept {
        return capacity_ - available_read();
    }

    [[nodiscard]] std::size_t push(const PlanarFrames source) noexcept {
        if (!source.valid() || source.channels != channels_ || capacity_ == 0) {
            return 0;
        }
        const auto count =
            source.frames < available_write() ? source.frames : available_write();
        const auto read = read_index_.load(std::memory_order_acquire);
        const auto write = write_index_.load(std::memory_order_relaxed);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const auto slot = (write + frame) % capacity_;
            for (std::size_t channel = 0; channel < channels_; ++channel) {
                samples_[slot * channels_ + channel] = source.planes[channel][frame];
            }
        }
        (void)read;
        write_index_.store(write + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t pop(const PlanarFrames destination) noexcept {
        if (!destination.valid() || destination.channels != channels_ || capacity_ == 0) {
            return 0;
        }
        const auto count =
            destination.frames < available_read() ? destination.frames : available_read();
        const auto read = read_index_.load(std::memory_order_relaxed);
        const auto write = write_index_.load(std::memory_order_acquire);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const auto slot = (read + frame) % capacity_;
            for (std::size_t channel = 0; channel < channels_; ++channel) {
                destination.planes[channel][frame] = samples_[slot * channels_ + channel];
            }
        }
        (void)write;
        read_index_.store(read + count, std::memory_order_release);
        return count;
    }

  private:
    // Cache-line padding separates the two atomics so producer and consumer
    // threads do not false-share. MSVC warns (C4324) on the implicit padding
    // that comes with alignas; that warning is the cost of avoiding the
    // worse cost of cache-line ping-pong on the audio RT path.
    const std::size_t capacity_;
    const std::size_t channels_;
    std::vector<float> samples_;
#ifdef _MSC_VER
    __declspec(align(64)) std::atomic<std::size_t> write_index_{0};
    __declspec(align(64)) std::atomic<std::size_t> read_index_{0};
#else
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
#endif
};

}  // namespace eclipse::audio
