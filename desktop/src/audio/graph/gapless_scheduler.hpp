// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "audio/ports/audio_types.hpp"
#include "audio/ports/idecoder.hpp"

namespace arrow::audio {

// ===========================================================================
//  TrackChanged event
// ===========================================================================

/// Event emitted when a new track becomes active.
struct TrackChanged final {
    /// 1-based index of the new track in the queue.
    std::size_t track_index = 0;
    /// Human-readable title of the new track.
    std::string title;
    /// Artist name.
    std::string artist;
    /// Album name.
    std::string album;
    /// Duration in seconds.
    double duration_s = 0.0;
};

/// Event emitted by the scheduler's RT callback.
using SchedulerEvent = TrackChanged;

// ===========================================================================
//  GaplessScheduler
// ===========================================================================

/// Time, in frames, over which we apply a crossfade at a track boundary when
/// formats differ or gapless metadata is unavailable (REQ-AUD-050 / REQ-AUD-051).
/// 40 ms at 44.1 kHz ≈ 1764 samples.
inline constexpr std::size_t kSpliceCrossfadeFrames = 1764;

/// Duration, in milliseconds, of the crossfade at track boundaries.
/// Used to compute kSpliceCrossfadeFrames for the current sample rate.
inline constexpr double kSpliceCrossfadeMs = 40.0;

/// The minimum number of frames that must be available in the incoming ring
/// before we begin the splice.  This prevents a late producer from causing a
/// glitch at the splice point.
inline constexpr std::size_t kMinSplicePreroll = 512;

/// Maximum number of tracks that can be queued at once.
inline constexpr std::size_t kMaxQueueSize = 64;

/// Abstract interface for the event ring that carries SchedulerEvent objects
/// from the RT callback to the UI / notification thread.
///
/// RT-SAFE: push() must be lock-free and allocation-free (the event must be
/// copied by value into pre-allocated storage).
class IEventRing {
  public:
    virtual ~IEventRing() = default;
    virtual void push(const SchedulerEvent& event) noexcept = 0;
    virtual bool pop(SchedulerEvent& out) noexcept = 0;
};

/// The gapless playback scheduler.
///
/// **Architecture** (spec §8.4):
/// - Holds two ring buffers (ring A and ring B) so that a track can be decoded
///   into ring B while ring A is being consumed.
/// - The RT audio callback reads from the currently-active ring.  At the splice
///   point, it switches from ring A to ring B mid-buffer.
/// - When formats differ between tracks, a 40 ms crossfade is applied at the
///   splice boundary (REQ-AUD-051).
/// - TrackChanged events are emitted via an IEventRing so the UI can update
///   without any locks in the RT path.
///
/// **Thread model**:
/// - RT-SAFE: pull() is safe to call from the RT audio callback.
/// - NOT RT-SAFE: enqueue_track(), skip(), stop(), reset() must only be called
///   from the decode worker thread.
///
/// **RAII**: all buffers are pre-allocated at construction; no allocation occurs
/// during streaming (REQ-AUD-007).
class GaplessScheduler {
  public:
    /// Configuration for a scheduler instance.
    struct Config final {
        /// Capacity, in frames, of each track ring.  Default 2 s at 44.1 kHz.
        std::size_t ring_capacity_frames = 88200;
        /// Sample rate used to compute crossfade sample counts.
        std::uint32_t sample_rate = 44100;
        /// Number of channels.  Default 2 (stereo).
        std::size_t channels = 2;
        /// Maximum queue depth.  Tracks beyond this are rejected.
        std::size_t max_queue_size = kMaxQueueSize;
    };

    /// Constructs a scheduler with the given configuration.
    explicit GaplessScheduler(Config config);

    // Non-copyable, non-movable.
    GaplessScheduler(const GaplessScheduler&) = delete;
    GaplessScheduler& operator=(const GaplessScheduler&) = delete;
    GaplessScheduler(GaplessScheduler&&) = delete;
    GaplessScheduler& operator=(GaplessScheduler&&) = delete;

    // -------------------------------------------------------------------------
    //  Worker-thread API (NOT RT-safe)
    // -------------------------------------------------------------------------

    /// Enqueues `track` for gapless playback.  The scheduler will begin decoding
    /// it into the inactive ring as soon as space is available.
    ///
    /// Returns the 1-based queue position on success; 0 on queue-full.
    [[nodiscard]] std::size_t enqueue_track(IDecoder& decoder) noexcept;

    /// Clears the queue and resets both rings.  Safe to call from any thread
    /// that is not concurrently calling pull().
    void reset() noexcept;

    /// Sets the event ring for TrackChanged notifications.
    /// A null pointer disables event emission.
    void set_event_ring(IEventRing* ring) noexcept { event_ring_ = ring; }

    // -------------------------------------------------------------------------
    //  RT audio callback — RT-SAFE
    // -------------------------------------------------------------------------

    /// Pulls up to `destination.frames` frames into `destination`.  This is the
    /// function passed as the AudioCallback to an IAudioSink.
    ///
    /// RT-SAFE: no locks, no syscalls, no heap allocation.
    void pull(PlanarFrames destination) noexcept;

    // -------------------------------------------------------------------------
    //  State queries
    // -------------------------------------------------------------------------

    /// Number of tracks currently queued (including the playing track).
    [[nodiscard]] std::size_t queue_size() const noexcept {
        return queue_size_.load(std::memory_order_relaxed);
    }

    /// True when the scheduler has no more audio to play.
    [[nodiscard]] bool at_end() const noexcept { return at_end_.load(std::memory_order_acquire); }

    /// True when the incoming ring has enough data to begin a splice.
    [[nodiscard]] bool can_splice() const noexcept;

    /// The number of frames currently available to read from the active ring.
    [[nodiscard]] std::size_t available_frames() const noexcept;

    // -------------------------------------------------------------------------
    //  Splice control — called from the worker thread
    // -------------------------------------------------------------------------

    /// Performs the splice: atomically switches the active ring so that the next
    /// pull() call reads from the incoming ring instead of the outgoing ring.
    /// The caller is responsible for ensuring can_splice() is true.
    ///
    /// NOT RT-safe: must only be called from the worker thread.
    void perform_splice() noexcept;

    /// Returns the crossfade frame count for the current sample rate.
    [[nodiscard]] std::size_t crossfade_frames() const noexcept {
        return crossfade_frames_;
    }

    /// The format of the currently-playing track.
    [[nodiscard]] std::optional<PcmFormat> active_format() const noexcept {
        return active_format_;
    }

    /// The format of the incoming (next) track.
    [[nodiscard]] std::optional<PcmFormat> incoming_format() const noexcept {
        return incoming_format_;
    }

  private:
    // -------------------------------------------------------------------------
    //  Constants
    // -------------------------------------------------------------------------
    const Config config_;

    /// Crossfade duration in frames, computed from config_.sample_rate.
    const std::size_t crossfade_frames_;

    // -------------------------------------------------------------------------
    //  Pre-allocated ring buffers
    // -------------------------------------------------------------------------
    std::vector<float> ring_a_;
    std::vector<float> ring_b_;
    std::vector<float*> ring_a_ptrs_;
    std::vector<float*> ring_b_ptrs_;

    /// Number of valid frames in each ring.
    std::atomic<std::size_t> ring_a_count_{0};
    std::atomic<std::size_t> ring_b_count_{0};

    /// Read cursor within each ring (consumer side).
    std::atomic<std::size_t> ring_a_read_{0};
    std::atomic<std::size_t> ring_b_read_{0};

    /// Write cursor within each ring (producer side — written by worker thread only).
    std::atomic<std::size_t> ring_a_write_{0};
    std::atomic<std::size_t> ring_b_write_{0};

    // -------------------------------------------------------------------------
    //  Splice state
    // -------------------------------------------------------------------------

    /// Which ring is currently active (0 = A, 1 = B).  Updated by perform_splice()
    /// and read by pull() with relaxed ordering — the splice is a full barrier.
    std::atomic<unsigned> active_ring_{0};

    /// True when the active ring has reached its end and a splice is pending.
    std::atomic<bool> splice_pending_{false};

    /// True when the two tracks have the same format (no crossfade needed).
    std::atomic<bool> formats_match_{true};

    /// Position within the crossfade (frames consumed so far in the fade).
    std::atomic<std::size_t> crossfade_pos_{0};

    /// Formats of the active and incoming tracks.
    std::optional<PcmFormat> active_format_;
    std::optional<PcmFormat> incoming_format_;

    // -------------------------------------------------------------------------
    //  Queue
    // -------------------------------------------------------------------------
    struct QueuedTrack {
        std::vector<std::vector<float>> decode_planes;
        std::vector<float*> plane_ptrs;
        PcmFormat format{};
        TrackChanged metadata{};

        // State that needs to be accessed from multiple threads. Separated into
        // its own struct so we can provide an explicit move constructor that
        // delegates to the copy semantics — std::atomic<T> is copyable but
        // libstdc++'s move constructor is deleted, so we explicitly delegate.
        struct State {
            std::atomic<std::size_t> written{0};
            std::atomic<bool> finished{false};
            std::atomic<std::size_t> read_cursor{0};

            State() = default;
            State(const State& o) : written(o.written.load()), finished(o.finished.load()),
                                   read_cursor(o.read_cursor.load()) {}
            State(State&& o) noexcept : written(o.written.load()), finished(o.finished.load()),
                                       read_cursor(o.read_cursor.load()) {}
            State& operator=(const State& o) noexcept {
                written.store(o.written.load());
                finished.store(o.finished.load());
                read_cursor.store(o.read_cursor.load());
                return *this;
            }
            State& operator=(State&& o) noexcept { return operator=(o); }
        };

        State state{};
    };

    std::vector<QueuedTrack> queue_;
    std::atomic<std::size_t> queue_size_{0};
    std::atomic<std::size_t> active_track_index_{0};
    std::atomic<std::size_t> next_write_ring_{0};  // 0 = A, 1 = B

    /// Pointer to receive TrackChanged events.  Null means "don't emit".
    IEventRing* event_ring_ = nullptr;

    /// Pre-allocated crossfade buffer (size = crossfade_frames_ × channels).
    std::vector<float> crossfade_buf_;

    // -------------------------------------------------------------------------
    //  End-of-stream state
    // -------------------------------------------------------------------------
    std::atomic<bool> at_end_{false};
    std::atomic<bool> at_end_crossfade_active_{false};
    std::atomic<std::size_t> at_end_crossfade_pos_{0};

    // -------------------------------------------------------------------------
    //  Internal helpers
    // -------------------------------------------------------------------------

    /// Computes the crossfade sample for a splice at `splice_pos` frames into
    /// `destination`, starting a `crossfade_frames_`-sample linear crossfade
    /// between the outgoing and incoming tracks.
    void apply_crossfade(
        PlanarFrames destination,
        std::size_t frames_to_write,
        std::size_t splice_pos,
        const std::vector<float*>& incoming_ptrs) noexcept;

    /// Returns a PlanarFrames view into the specified ring.
    PlanarFrames ring_view(
        const std::vector<float>& ring,
        const std::vector<float*>& ptrs,
        std::size_t available) const noexcept;
};

// ===========================================================================
//  Inline helpers
// ===========================================================================

/// Returns the sample count for a crossfade of `ms` milliseconds at `rate` Hz.
[[nodiscard]] inline std::size_t crossfade_frames_for(
    double ms, std::uint32_t rate) noexcept {
    return static_cast<std::size_t>(ms * static_cast<double>(rate) / 1000.0 + 0.5);
}

}  // namespace arrow::audio
