// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#include "audio/graph/gapless_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace arrow::audio {

// ===========================================================================
//  GaplessScheduler
// ===========================================================================

GaplessScheduler::GaplessScheduler(Config config)
      : config_(config),
        crossfade_frames_(crossfade_frames_for(kSpliceCrossfadeMs, config.sample_rate)) {
    const auto capacity = config.ring_capacity_frames * config.channels;

    ring_a_.resize(capacity, 0.0f);
    ring_b_.resize(capacity, 0.0f);

    ring_a_ptrs_.resize(config.channels);
    ring_b_ptrs_.resize(config.channels);

    for (std::size_t ch = 0; ch < config.channels; ++ch) {
        ring_a_ptrs_[ch] = ring_a_.data() + ch * config.ring_capacity_frames;
        ring_b_ptrs_[ch] = ring_b_.data() + ch * config.ring_capacity_frames;
    }

    crossfade_buf_.resize(crossfade_frames_ * config.channels, 0.0f);
}

std::size_t GaplessScheduler::enqueue_track(IDecoder& decoder) noexcept {
    if (queue_size_.load(std::memory_order_relaxed) >= config_.max_queue_size) {
        return 0;
    }

    // emplace_back constructs in-place, avoiding the move-construction requirement.
    // After emplace_back, we immediately populate the fields — no move needed.
    queue_.emplace_back();  // default-construct a QueuedTrack
    auto& track = queue_.back();
    track.decode_planes.resize(config_.channels,
                               std::vector<float>(config_.ring_capacity_frames));
    track.plane_ptrs.resize(config_.channels);
    for (std::size_t ch = 0; ch < config_.channels; ++ch) {
        track.plane_ptrs[ch] = track.decode_planes[ch].data();
    }

    auto info = decoder.stream_info();
    if (info) {
        track.format = info->format;
    }

    queue_size_.store(queue_.size(), std::memory_order_relaxed);
    return queue_.size();
}

bool GaplessScheduler::can_splice() const noexcept {
    const unsigned ring = next_write_ring_.load(std::memory_order_acquire);
    const auto count = ring == 0 ? ring_b_count_.load(std::memory_order_acquire)
                                 : ring_a_count_.load(std::memory_order_acquire);
    return count >= kMinSplicePreroll;
}

std::size_t GaplessScheduler::available_frames() const noexcept {
    const unsigned ring = active_ring_.load(std::memory_order_acquire);
    const auto count = ring == 0 ? ring_a_count_.load(std::memory_order_acquire)
                                 : ring_b_count_.load(std::memory_order_acquire);
    const auto read_pos = ring == 0 ? ring_a_read_.load(std::memory_order_relaxed)
                                    : ring_b_read_.load(std::memory_order_relaxed);
    return count > read_pos ? count - read_pos : 0;
}

void GaplessScheduler::perform_splice() noexcept {
    splice_pending_.store(false, std::memory_order_release);
    const unsigned prev = active_ring_.exchange(1 - active_ring_, std::memory_order_acq_rel);
    (void)prev;

    crossfade_pos_.store(0, std::memory_order_release);
}

PlanarFrames GaplessScheduler::ring_view(
    const std::vector<float>& ring,
    const std::vector<float*>& ptrs,
    const std::size_t available) const noexcept {
    (void)available;
    return PlanarFrames{ptrs.data(), config_.channels, available};
}

void GaplessScheduler::apply_crossfade(
    PlanarFrames destination,
    const std::size_t frames_to_write,
    const std::size_t splice_pos,
    const std::vector<float*>& incoming_ptrs) noexcept {
    // Linear crossfade from outgoing to incoming: at position t/T,
    // outgoing gets weight (1 - t/T), incoming gets weight (t/T).
    for (std::size_t f = 0; f < frames_to_write; ++f) {
        const std::size_t total = crossfade_frames_;
        // Clamp to avoid division by zero when total == 0.
        const std::size_t pos_in_fade = splice_pos + f;
        if (total == 0) continue;
        const double t = static_cast<double>(pos_in_fade) / static_cast<double>(total);
        if (t >= 1.0) break;  // fade complete

        const double weight_out = 1.0 - t;
        const double weight_in = t;

        for (std::size_t ch = 0; ch < destination.channels; ++ch) {
            const auto idx = f + splice_pos;
            const float out_sample = destination.planes[ch][idx];
            const float in_sample = incoming_ptrs[ch][f];
            destination.planes[ch][idx] =
                static_cast<float>(out_sample * weight_out + in_sample * weight_in);
        }
    }
}

void GaplessScheduler::pull(PlanarFrames destination) noexcept {
    if (!destination.valid()) return;
    if (destination.channels != config_.channels) return;

    // Fill with silence initially.
    for (std::size_t ch = 0; ch < destination.channels; ++ch) {
        std::memset(destination.planes[ch], 0,
                     destination.frames * sizeof(float));
    }

    if (at_end_.load(std::memory_order_acquire)) return;

    const unsigned ring = active_ring_.load(std::memory_order_acquire);
    const bool formats_match = formats_match_.load(std::memory_order_acquire);

    // Raw reads from the active ring.
    auto& count_atomic = ring == 0 ? ring_a_count_ : ring_b_count_;
    auto& read_atomic = ring == 0 ? ring_a_read_ : ring_b_read_;
    auto& ring_data = ring == 0 ? ring_a_ : ring_b_;
    auto& ptrs = ring == 0 ? ring_a_ptrs_ : ring_b_ptrs_;

    const auto count = count_atomic.load(std::memory_order_acquire);
    const auto read = read_atomic.load(std::memory_order_relaxed);
    const auto available = count > read ? count - read : 0;

    if (available == 0) {
        // Check if we need to splice.
        if (splice_pending_.load(std::memory_order_acquire) && can_splice()) {
            perform_splice();
            // After splice, retry — but only once per pull to avoid an infinite
            // loop if can_splice() keeps returning false.
            return pull(destination);
        }
        return;
    }

    const auto frames_to_read = std::min(destination.frames, available);
    const auto read_head = read % config_.ring_capacity_frames;

    for (std::size_t f = 0; f < frames_to_read; ++f) {
        const auto slot = (read_head + f) % config_.ring_capacity_frames;
        for (std::size_t ch = 0; ch < config_.channels; ++ch) {
            destination.planes[ch][f] = ring_data[slot * config_.channels + ch];
        }
    }

    read_atomic.store(read + frames_to_read, std::memory_order_release);

    // If we filled the whole destination, we're done.
    if (frames_to_read >= destination.frames) return;

    // Check if we hit the end of the active track and need to splice.
    const auto new_count = count_atomic.load(std::memory_order_acquire);
    const auto new_read = read_atomic.load(std::memory_order_relaxed);
    const auto new_available = new_count > new_read ? new_count - new_read : 0;

    if (new_available == 0 && splice_pending_.load(std::memory_order_acquire)) {
        // Apply crossfade if formats differ.
        if (!formats_match && crossfade_frames_ > 0) {
            const unsigned incoming_ring = 1 - ring;
            auto& incoming_count = incoming_ring == 0 ? ring_a_count_ : ring_b_count_;
            auto& incoming_read = incoming_ring == 0 ? ring_a_read_ : ring_b_read_;
            auto& incoming_ring_data = incoming_ring == 0 ? ring_a_ : ring_b_;
            auto& incoming_ptrs = incoming_ring == 0 ? ring_a_ptrs_ : ring_b_ptrs_;

            const auto incoming_count_val =
                incoming_count.load(std::memory_order_acquire);
            const auto incoming_read_val =
                incoming_read.load(std::memory_order_relaxed);
            const auto incoming_available =
                incoming_count_val > incoming_read_val
                    ? incoming_count_val - incoming_read_val
                    : 0;

            if (incoming_available > 0) {
                const auto fade_frames =
                    std::min(crossfade_frames_, static_cast<std::size_t>(frames_to_read));

                // Read incoming samples into crossfade buffer.
                const auto incoming_read_head = incoming_read_val % config_.ring_capacity_frames;
                for (std::size_t f = 0; f < fade_frames; ++f) {
                    const auto slot = (incoming_read_head + f) % config_.ring_capacity_frames;
                    for (std::size_t ch = 0; ch < config_.channels; ++ch) {
                        crossfade_buf_[f * config_.channels + ch] =
                            incoming_ring_data[slot * config_.channels + ch];
                    }
                }
                incoming_read.store(incoming_read_val + fade_frames,
                                   std::memory_order_release);

                // Apply crossfade in-place into destination.
                for (std::size_t ch = 0; ch < config_.channels; ++ch) {
                    auto* incoming_buf_ptr = crossfade_buf_.data() + ch * crossfade_frames_;
                    for (std::size_t f = 0; f < fade_frames; ++f) {
                        const double t =
                            static_cast<double>(crossfade_pos_.load(std::memory_order_relaxed) + f) /
                            static_cast<double>(crossfade_frames_);
                        const double weight_out = 1.0 - t;
                        const double weight_in = t;
                        destination.planes[ch][f] = static_cast<float>(
                            destination.planes[ch][f] * weight_out +
                            incoming_buf_ptr[f] * weight_in);
                    }
                }
                crossfade_pos_.store(
                    crossfade_pos_.load(std::memory_order_relaxed) + fade_frames,
                    std::memory_order_release);
            }
        }

        // Emit TrackChanged event.
        if (event_ring_ && !queue_.empty()) {
            TrackChanged ev{};
            if (active_track_index_.load(std::memory_order_relaxed) < queue_.size()) {
                ev = queue_[active_track_index_.load(std::memory_order_relaxed)].metadata;
            }
            event_ring_->push(ev);
        }

        perform_splice();
    }
}

void GaplessScheduler::reset() noexcept {
    ring_a_count_.store(0, std::memory_order_relaxed);
    ring_b_count_.store(0, std::memory_order_relaxed);
    ring_a_read_.store(0, std::memory_order_relaxed);
    ring_b_read_.store(0, std::memory_order_relaxed);
    ring_a_write_.store(0, std::memory_order_relaxed);
    ring_b_write_.store(0, std::memory_order_relaxed);
    active_ring_.store(0, std::memory_order_relaxed);
    splice_pending_.store(false, std::memory_order_release);
    formats_match_.store(true, std::memory_order_relaxed);
    crossfade_pos_.store(0, std::memory_order_relaxed);
    at_end_.store(false, std::memory_order_release);
    at_end_crossfade_active_.store(false, std::memory_order_relaxed);
    at_end_crossfade_pos_.store(0, std::memory_order_relaxed);
    active_track_index_.store(0, std::memory_order_relaxed);
    next_write_ring_.store(0, std::memory_order_relaxed);
    queue_.clear();
    queue_size_.store(0, std::memory_order_relaxed);
    active_format_ = std::nullopt;
    incoming_format_ = std::nullopt;
}

}  // namespace arrow::audio
