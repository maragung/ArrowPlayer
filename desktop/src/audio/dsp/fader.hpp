// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>

#include "audio/ports/audio_types.hpp"
#include "core/error.hpp"

namespace arrow::audio {

// ===========================================================================
//  Fade curve types — spec §8.9.3 (REQ-AUD-050 .. REQ-AUD-054)
// ===========================================================================

/// Curve shapes for fades.
enum class FadeCurve {
    Linear,       ///< REQ-AUD-050: equal-power linear fade
    EqualPower,  ///< REQ-AUD-051: equal-power (sine/cosine) fade
    Exponential,  ///< REQ-AUD-052: exponential decay / attack
    Logarithmic,  ///< REQ-AUD-053: logarithmic (human-ear model)
    SCurve,      ///< REQ-AUD-054: S-curve for gentle fade-in/out
};

/// Direction of a fade.
enum class FadeDirection { In, Out };

/// Specification for a single fade.
struct FadeSpec {
    FadeCurve curve = FadeCurve::EqualPower;
    FadeDirection direction = FadeDirection::Out;
    /// Fade duration in frames.  0 = instant.
    std::size_t duration_frames = 0;
    /// Position of the fade relative to the region it modifies.
    ///   0 = fade at start
    ///   1 = fade at end
    ///   in-between = split fade
    std::size_t split_position = 0;
};

/// A fade-in/fade-out pair for a track.
struct TrackFade final {
    FadeSpec fade_in;
    FadeSpec fade_out;
};

// ===========================================================================
//  FaderStage — RT-safe fade controller
// ===========================================================================

/// A per-channel fade controller that generates gain values for use by the DSP
/// chain's fade stage.
///
/// The FaderStage does NOT apply the fade itself — it is a gain envelope
/// generator. The caller multiplies each sample by `gain()` or applies the
/// envelope buffer returned by `gain_buffer()`.
///
/// RT-SAFE: all methods allocate nothing and lock nothing.
///
/// True bypass: when no fade is in progress and `set_fade()` has not been
/// called, `gain()` always returns 1.0f (REQ-AUD-005).
class FaderStage {
  public:
    FaderStage() noexcept = default;

    // Non-copyable (has state).
    FaderStage(const FaderStage&) = delete;
    FaderStage& operator=(const FaderStage&) = delete;
    FaderStage(FaderStage&&) = delete;
    FaderStage& operator=(FaderStage&&) = delete;

    // -------------------------------------------------------------------------
    //  Configuration — NOT RT-safe
    // -------------------------------------------------------------------------

    /// Sets the target fade parameters.  The fade begins immediately.
    /// A call to set_fade() with duration_frames == 0 cancels any in-progress
    /// fade and returns the fader to unity gain.
    void set_fade(FadeSpec spec) noexcept;

    /// Immediately cancels any in-progress fade, returning gain to 1.0.
    void cancel() noexcept;

    // -------------------------------------------------------------------------
    //  RT-safe processing
    // -------------------------------------------------------------------------

    /// Returns the current linear gain (0.0 to 1.0+).  Call once per frame
    /// when processing sample-by-sample, or use gain_buffer() for block processing.
    [[nodiscard]] float gain() const noexcept {
        return gain_.load(std::memory_order_relaxed);
    }

    /// Advances the fade by one frame, returning the new gain value.
    /// Call this once per output frame in the DSP chain.
    [[nodiscard]] float tick() noexcept {
        if (!active_.load(std::memory_order_acquire)) return 1.0f;

        const auto remaining = remaining_.load(std::memory_order_relaxed);
        if (remaining == 0) {
            active_.store(false, std::memory_order_release);
            gain_.store(1.0f, std::memory_order_relaxed);
            return 1.0f;
        }

        // Advance: each tick decrements remaining by 1.
        remaining_.store(remaining - 1, std::memory_order_relaxed);

        const auto total = total_.load(std::memory_order_relaxed);
        const double t = 1.0 - static_cast<double>(remaining - 1) / static_cast<double>(total);
        const float new_gain = compute_gain(t);
        gain_.store(new_gain, std::memory_order_relaxed);
        return new_gain;
    }

    /// Returns true when a fade is currently in progress.
    [[nodiscard]] bool active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    /// Resets the fader to unity gain.  Cancels any in-progress fade.
    void reset() noexcept {
        active_.store(false, std::memory_order_relaxed);
        remaining_.store(0, std::memory_order_relaxed);
        total_.store(0, std::memory_order_relaxed);
        gain_.store(1.0f, std::memory_order_relaxed);
    }

    /// Generates a gain buffer for block processing.
    /// Populates `out` with the gain values for the next `out.size()` frames.
    void gain_buffer(std::span<float> out) noexcept;

    // -------------------------------------------------------------------------
    //  Curve computation — RT-safe
    // -------------------------------------------------------------------------

    /// Computes the gain for position `t` (0.0 to 1.0) using the specified curve.
    [[nodiscard]] static float compute_gain(
        FadeCurve curve, FadeDirection direction, double t) noexcept;

    [[nodiscard]] static float compute_gain(double t) noexcept {
        return compute_gain(FadeCurve::EqualPower, FadeDirection::Out, t);
    }

  private:
    std::atomic<bool> active_{false};
    std::atomic<std::size_t> remaining_{0};
    std::atomic<std::size_t> total_{0};
    std::atomic<FadeCurve> curve_{FadeCurve::EqualPower};
    std::atomic<FadeDirection> direction_{FadeDirection::Out};
    std::atomic<float> gain_{1.0f};
};

// ===========================================================================
//  Fade helpers
// ===========================================================================

/// Linear interpolation between two values.
[[nodiscard]] inline float lerpf(float a, float b, float t) noexcept {
    return a + (b - a) * t;
}

/// Clamps a value to [0, 1].
[[nodiscard]] inline float clamp01(float v) noexcept {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

}  // namespace arrow::audio
