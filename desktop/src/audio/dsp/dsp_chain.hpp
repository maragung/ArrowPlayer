// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "audio/dsp/biquad.hpp"
#include "audio/dsp/equalizer.hpp"
#include "audio/dsp/fader.hpp"
#include "audio/dsp/limiter.hpp"
#include "audio/dsp/replaygain.hpp"
#include "audio/ports/audio_types.hpp"
#include "core/error.hpp"

namespace arrow::audio {

// ===========================================================================
//  Canonical signal chain — spec §8.9
//
//  Stage order (spec §8.9):
//    1.  Volume / mute
//    2.  ReplayGain pre-amp
//    3.  Equalizer (pre-amp + bands)
//    4.  Sample-rate conversion
//    5.  Channel remapping / upmix
//    6.  Dithering
//    7.  Speaker correction EQ
//    8.  Volume limiter
//    9.  Gapless output gate
//   10.  Crossfade (gapless splice)
//   11.  Output volume + fade
//
//  Stages 3 (EQ), 8 (limiter), 10 (crossfade), 11 (fader) are implemented here.
//  Stage 4 (SRC) is handled by the resampler in playback_graph.
//  Stage 5 (remap) is handled by the channel remapper in playback_graph.
//  Stage 6 (dither) is a simple TPDF dither in the resampler.
//  Stage 7 (speaker correction) is a separate EQ stage.
// ===========================================================================

/// Configuration for one DSP stage.
struct DspStageConfig {
    std::size_t channels = 2;
    double sample_rate = 44100.0;
};

/// Result of a configure() call — includes the sample rate actually configured.
struct DspChainResult {
    double sample_rate = 44100.0;
    std::size_t channels = 2;
};

/// The complete DSP signal chain.
///
/// Ownership/threading: configure() is NOT RT-safe (allocates and computes
/// coefficients). process() IS RT-safe.
///
/// RAII: all buffers are pre-allocated at construction; no allocation during
/// streaming (REQ-AUD-007).
///
/// True bypass: when is_bypassed() returns true, process() leaves the buffer
/// exactly as it received it (REQ-AUD-005).
class DspChain {
  public:
    /// Constructs an empty chain.  Call configure() before processing.
    DspChain() = default;

    // Non-copyable (holds per-channel state).
    DspChain(const DspChain&) = delete;
    DspChain& operator=(const DspChain&) = delete;
    DspChain(DspChain&&) = delete;
    DspChain& operator=(DspChain&&) = delete;

    // -------------------------------------------------------------------------
    //  Configuration — NOT RT-safe
    // -------------------------------------------------------------------------

    /// Configures the entire chain for `channels` channels at `sample_rate_hz`.
    /// Call this once when starting playback or when the format changes.
    ///
    /// \param config  Channel count and sample rate.
    /// \return ok() on success; Error on failure.
    [[nodiscard]] Status configure(const DspStageConfig& config);

    /// Applies a new EQ setting and starts the 32 ms cross-ramp (REQ-AUD-085).
    /// NOT RT-safe.
    void apply_eq(const EqSettings& settings);

    /// Applies ReplayGain settings.  NOT RT-safe.
    void apply_replaygain(const ReplayGainSettings& settings);

    /// Sets the output volume (0.0 to 1.0+).  NOT RT-safe (stores a value
    /// consumed by the RT fader).
    void set_volume(float volume) noexcept { volume_.store(volume, std::memory_order_relaxed); }

    /// Sets a fade.  NOT RT-safe.
    void set_fade(FadeSpec spec) noexcept { fader_.set_fade(spec); }

    /// Cancels any in-progress fade.  NOT RT-safe.
    void cancel_fade() noexcept { fader_.cancel(); }

    /// Sets the limiter ceiling.  NOT RT-safe.
    void set_limiter_ceiling(double ceiling_db) noexcept {
        limiter_.set_ceiling(ceiling_db);
    }

    // -------------------------------------------------------------------------
    //  RT-safe processing
    // -------------------------------------------------------------------------

    /// Processes all stages in order.  The EQ is applied first, then the limiter,
    /// then the fader, then the volume.  Each stage is skipped when bypassed.
    ///
    /// RT-SAFE: no allocation, no locking, no syscalls.
    ///
    /// \param planes  Multi-channel planar float buffer to process in place.
    void process(std::span<std::span<float>> planes) noexcept;

    /// Single-channel overload.
    void process(std::span<float> plane) noexcept {
        if (bypassed_) return;
        if (eq_.is_bypassed()) {
            // EQ bypassed: just do volume/fade/limiter
        } else {
            std::span<std::span<float>> sp{&plane, 1};
            process(sp);
            return;
        }
    }

    /// Clears all filter delay lines.  Call on seek and track change.
    void reset() noexcept;

    // -------------------------------------------------------------------------
    //  State queries
    // -------------------------------------------------------------------------

    /// True when the entire chain is audibly a no-op (every stage is bypassed).
    [[nodiscard]] bool is_bypassed() const noexcept { return bypassed_; }

    /// True when the EQ is currently mid-ramp (REQ-AUD-085).
    [[nodiscard]] bool eq_ramping() const noexcept { return eq_.ramping(); }

    /// Largest EQ ramp remaining, in samples.  0 when settled.
    [[nodiscard]] std::size_t eq_ramp_remaining() const noexcept {
        return eq_.ramp_remaining();
    }

    /// True when a fade is in progress.
    [[nodiscard]] bool fade_active() const noexcept { return fader_.active(); }

    // -------------------------------------------------------------------------
    //  Individual stage access — for testing and diagnostics
    // -------------------------------------------------------------------------

    [[nodiscard]] const Equalizer& eq() const noexcept { return eq_; }
    [[nodiscard]] const LimiterStage& limiter() const noexcept { return limiter_; }
    [[nodiscard]] const FaderStage& fader() const noexcept { return fader_; }

  private:
    Equalizer eq_;
    LimiterStage limiter_;
    FaderStage fader_;

    DspStageConfig config_{};
    std::atomic<float> volume_{1.0f};
    ReplayGainSettings rg_settings_{};
    bool configured_{false};
    bool bypassed_{true};
};

}  // namespace arrow::audio
