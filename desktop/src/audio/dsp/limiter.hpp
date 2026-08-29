// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace arrow::audio {

// ===========================================================================
//  Constants — spec §8.9.8 (REQ-AUD-110, REQ-AUD-111)
// ===========================================================================

/// Default ceiling in dBFS.  -0.3 dB ≈ -0.3 dBTP as required by REQ-AUD-111.
inline constexpr double kLimiterCeilingDb = -0.3;

/// Minimum ceiling to prevent infinite gain reduction.
inline constexpr double kLimiterCeilingMinDb = -20.0;

/// Maximum ceiling (0 dBFS = no limiting, pass-through).
inline constexpr double kLimiterCeilingMaxDb = 0.0;

/// Default release time constant in ms (REQ-AUD-112).
inline constexpr double kLimiterReleaseMs = 50.0;

/// Look-ahead duration in ms — must be positive for true-peak detection.
/// Default 5 ms as used by the ITU-R BS.1770 limiter reference.
inline constexpr double kLimiterLookAheadMs = 5.0;

/// Maximum look-ahead in milliseconds (for sanity).
inline constexpr double kLimiterLookAheadMaxMs = 20.0;

// ===========================================================================
//  LimiterStage
// ===========================================================================

/// A look-ahead true-peak limiter.
///
/// This stage operates in two phases per buffer:
///   1. Look-ahead pass: compute the peak over each window of `lookAhead`
///      samples, storing the maximum absolute sample per window in `peak_buf_`.
///   2. Gain pass: for each sample, look at the peak of the window starting
///      at that sample, compute the required gain reduction, apply it, and
///      smoothly update the gain factor with an exponential release.
///
/// The result is that the output never exceeds the ceiling, and the gain
/// reduction is released smoothly (no abrupt jumps when the signal falls
/// below threshold).
///
/// RT-SAFE: all methods allocate nothing and lock nothing.  The
/// coefficient-design functions (`configure`, `set_ceiling`) are NOT RT-safe.
///
/// True bypass: when `is_bypassed()` returns true, the stage leaves the buffer
/// exactly as it received it (REQ-AUD-005).
class LimiterStage {
  public:
    /// Constructs an unconfigured limiter.  Call configure() before processing.
    LimiterStage() noexcept = default;

    // Non-copyable (holds state).
    LimiterStage(const LimiterStage&) = delete;
    LimiterStage& operator=(const LimiterStage&) = delete;

    // -------------------------------------------------------------------------
    //  Configuration — NOT RT-safe
    // -------------------------------------------------------------------------

    /// Configures the limiter for `sample_rate_hz`.  Must be called before any
    /// process() call.  NOT RT-safe (computes transcendental coefficients).
    ///
    /// \param sample_rate_hz  Output sample rate in Hz.
    /// \param ceiling_db      Maximum output level in dBFS.
    [[nodiscard]] Status configure(double sample_rate_hz, double ceiling_db = kLimiterCeilingDb);

    /// Changes the ceiling at runtime.  The next process() call will immediately
    /// use the new ceiling.  NOT RT-safe.
    void set_ceiling(double ceiling_db) noexcept;

    /// Returns the current ceiling in dBFS.
    [[nodiscard]] double ceiling_db() const noexcept { return ceiling_db_; }

    /// Returns the look-ahead in samples.
    [[nodiscard]] std::size_t look_ahead_samples() const noexcept { return look_ahead_; }

    /// Returns true when configure() has been called successfully.
    [[nodiscard]] bool is_configured() const noexcept { return configured_; }

    /// Returns true when the limiter is audibly a no-op (ceiling >= 0 dBFS).
    [[nodiscard]] bool is_bypassed() const noexcept { return bypassed_; }

    // -------------------------------------------------------------------------
    //  State management
    // -------------------------------------------------------------------------

    /// Resets the release-state.  Call on seek and track change so the limiter
    /// does not drag a low gain from the end of one track into the start of
    /// the next.
    void reset() noexcept;

    // -------------------------------------------------------------------------
    //  RT-safe processing
    // -------------------------------------------------------------------------

    /// Processes a single channel in place.  The limiter always sees all channels
    /// simultaneously for true-peak detection; call this for each channel with
    /// the same `peak_buf` shared across all channels.
    ///
    /// RT-SAFE: no allocation, no locking.
    void process_channel(
        std::span<float> buffer,
        std::span<double> peak_buf) noexcept;

    /// Multi-channel true-peak limiter.  Processes `channels` planes in place.
    /// This is the canonical RT entry point.
    ///
    /// The limiter performs look-ahead detection across all channels simultaneously,
    /// computing the maximum absolute value across channels for each sample position.
    ///
    /// RT-SAFE: no allocation, no locking.
    void process(std::span<std::span<float>> planes) noexcept;

    /// Single-channel overload for downstream callers.
    void process(std::span<float> buffer) noexcept;

  private:
    /// Computes the per-sample look-ahead peak for the current buffer.
    /// Populates `peak_buf` with the maximum absolute sample over each look-ahead window.
    void compute_peaks(std::span<const float> buffer, std::span<double> peak_buf) const noexcept;

    /// Applies the gain reduction to one channel.
    void apply_gain(std::span<float> buffer, std::span<const double> peak_buf) noexcept;

    double sample_rate_{48000.0};
    double ceiling_db_{-0.3};
    double ceiling_linear_{1.0};
    double release_coeff_{0.999};  // gain multiplier per sample
    std::size_t look_ahead_{240};  // 5 ms at 48 kHz
    bool configured_{false};
    bool bypassed_{false};

    /// Per-channel gain state for exponential release.
    std::vector<double> gain_state_;
    std::size_t channels_{0};

    /// Pre-allocated working buffer for peak detection.
    std::vector<double> peak_buf_;
};

// ===========================================================================
//  Inline helpers
// ===========================================================================

/// Converts a linear amplitude to dBFS.
[[nodiscard]] inline double linear_to_dbfs(double linear) noexcept {
    if (linear <= 0.0) return -240.0;
    return 20.0 * std::log10(linear);
}

/// Converts dBFS to linear amplitude.
[[nodiscard]] inline double dbfs_to_linear(double dbfs) noexcept {
    return std::pow(10.0, dbfs / 20.0);
}

}  // namespace arrow::audio
