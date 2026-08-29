// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "audio/ports/audio_types.hpp"

namespace arrow::audio {

/// Preallocated, lock-free SPSC ring buffer of planar float frames.
///
/// Capacity is fixed at construction — no heap allocation occurs during
/// streaming (REQ-AUD-007).  Template parameters:
///
///   Frames  — number of frame slots (must be a power of two)
///   Channels — channel count for every frame stored
///
/// Producer (write) and consumer (read) indices live on separate cache lines
/// (REQ-AUD-013) so the audio RT thread and the decoder worker thread never
/// false-share.  All atomic operations use explicit memory_order_acquire /
/// memory_order_release to satisfy REQ-AUD-014.
///
/// \warning Exactly one producer and one consumer are allowed concurrently.
///          Using this from more than one thread on either side is undefined
///          behaviour.
template<std::size_t Frames, std::size_t Channels>
class RingBuffer final {
    static_assert(Frames > 0, "RingBuffer capacity must be non-zero");
    static_assert(Channels > 0, "RingBuffer channel count must be non-zero");
    // The power-of-two requirement enables a fast branch-free modulo:
    // (pos & (Frames-1)) == (pos % Frames) when Frames is a power of two.
    static_assert((Frames & (Frames - 1)) == 0,
                  "RingBuffer capacity must be a power of two");

  public:
    /// Number of frames that fit in the ring.
    static constexpr std::size_t capacity = Frames;
    /// Number of channels per frame.
    static constexpr std::size_t channels = Channels;

    /// Constructs an empty ring.  All sample storage is pre-allocated now so
    /// that the RT path never touches the heap.
    RingBuffer() noexcept = default;

    // Non-copyable, non-movable — the buffer is a fixed-size resource.
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    // -------------------------------------------------------------------------
    //  Capacity / availability queries
    // -------------------------------------------------------------------------

    /// Total frame capacity of this ring.
    [[nodiscard]] static constexpr std::size_t frame_capacity() noexcept {
        return Frames;
    }

    /// Channel count of every stored frame.
    [[nodiscard]] static constexpr std::size_t channel_count() noexcept {
        return Channels;
    }

    /// Frames available to read — callable from both threads.
    [[nodiscard]] std::size_t available_read() const noexcept {
        const auto write = write_index_.load(std::memory_order_acquire);
        const auto read = read_index_.load(std::memory_order_relaxed);
        assert(write >= read);
        return write - read;
    }

    /// Frames available to write — callable from both threads.
    [[nodiscard]] std::size_t available_write() const noexcept {
        // available_read() is acquire, so we are consistent with it.
        return Frames - available_read();
    }

    // -------------------------------------------------------------------------
    //  Producer API — call from decoder thread only
    // -------------------------------------------------------------------------

    /// Copies up to `source.frames` frames from `source` into the ring.
    /// Returns the number of frames actually written (never more than
    /// available_write()).
    ///
    /// RT-SAFE: no allocation, no locking, no atomics that would serialize
    /// with the consumer on most architectures.
    [[nodiscard]] std::size_t push(const PlanarFrames& source) noexcept {
        if (!source.valid()) return 0;
        if (source.channels != Channels) return 0;

        const auto to_write =
            (source.frames < available_write()) ? source.frames : available_write();
        if (to_write == 0) return 0;

        const auto write_head = write_index_.load(std::memory_order_relaxed);
        for (std::size_t frame = 0; frame < to_write; ++frame) {
            const auto slot = (write_head + frame) & (Frames - 1);
            for (std::size_t ch = 0; ch < Channels; ++ch) {
                slots_[slot * Channels + ch] = source.planes[ch][frame];
            }
        }
        // Release so the consumer sees the complete frames after this store.
        write_index_.store(write_head + to_write, std::memory_order_release);
        return to_write;
    }

    // -------------------------------------------------------------------------
    //  Consumer API — call from RT audio thread only
    // -------------------------------------------------------------------------

    /// Copies up to `destination.frames` frames from the ring into
    /// `destination`.  Returns the number of frames actually read.
    ///
    /// RT-SAFE: no allocation, no locking.
    [[nodiscard]] std::size_t pop(const PlanarFrames& destination) noexcept {
        if (!destination.valid()) return 0;
        if (destination.channels != Channels) return 0;

        const auto to_read =
            (destination.frames < available_read()) ? destination.frames : available_read();
        if (to_read == 0) return 0;

        const auto read_head = read_index_.load(std::memory_order_relaxed);
        for (std::size_t frame = 0; frame < to_read; ++frame) {
            const auto slot = (read_head + frame) & (Frames - 1);
            for (std::size_t ch = 0; ch < Channels; ++ch) {
                destination.planes[ch][frame] = slots_[slot * Channels + ch];
            }
        }
        // Release so a subsequent push() from the producer can reuse the slot.
        read_index_.store(read_head + to_read, std::memory_order_release);
        return to_read;
    }

    /// Advances the read head by `frames` without reading any data.
    /// Useful when the RT thread discards trailing silence after a splice.
    void skip_read(std::size_t frames) noexcept {
        if (frames == 0) return;
        const auto read_head = read_index_.load(std::memory_order_relaxed);
        read_index_.store(read_head + frames, std::memory_order_release);
    }

    /// Advances the write head by `frames` without writing any data.
    /// Useful when the producer discards corrupted input.
    void skip_write(std::size_t frames) noexcept {
        if (frames == 0) return;
        const auto write_head = write_index_.load(std::memory_order_relaxed);
        write_index_.store(write_head + frames, std::memory_order_release);
    }

    /// Returns true when there are no readable frames.
    [[nodiscard]] bool empty() const noexcept { return available_read() == 0; }

    /// Returns true when there is no space to write.
    [[nodiscard]] bool full() const noexcept { return available_write() == 0; }

    /// Clears all slots and resets both indices to zero.
    /// Call from the producer thread when starting a new track.
    void reset() noexcept {
        write_index_.store(0, std::memory_order_relaxed);
        read_index_.store(0, std::memory_order_relaxed);
        slots_.assign(slots_.size(), 0.0f);
    }

  private:
    // -------------------------------------------------------------------------
    //  Storage — one flat float array: [frame0ch0, frame0ch1, ..., frameNch0, ...]
    // -------------------------------------------------------------------------
    std::vector<float> slots_{Frames * Channels, 0.0f};

    // -------------------------------------------------------------------------
    //  Cache-line-padded indices — eliminates false sharing (REQ-AUD-013).
    // -------------------------------------------------------------------------
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // structure was padded due to alignment spec
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
#pragma warning(pop)
#else
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
#endif
};

}  // namespace arrow::audio
