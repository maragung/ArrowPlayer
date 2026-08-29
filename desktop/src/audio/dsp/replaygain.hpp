// SPDX-License-Identifier: MPL-2.0
// ReplayGain 2.0 — spec §8.9.4 (REQ-AUD-100 .. REQ-AUD-104).
//
// The standard is ITU-R BS.1770-4 with reference -18 LUFS (REQ-AUD-100). The
// measurement pipeline is:
//
//   PCM in  ->  K-weighting (pre-filter + RLB)  ->  per-channel mean square
//           ->  400 ms blocks, 75% overlap     ->  gating (-70 LUFS abs,
//                                                  -10 LU relative)
//           ->  integrated loudness            ->  ReplayGain value
//
// The K-weighting filter is the same for every implementation; the
// coefficients here are the ones ITU-R BS.1770-4 Annex 1 specifies
// explicitly. They are the only legal values: any other choice is no longer
// a BS.1770-4 measurement.
//
// This file deliberately has no analysis-side dependencies (no FFT, no
// resampler): the loudness is summed directly from squared samples, which
// is the energy term inside the LUFS equation. The block sum, the gating
// and the integrated-loudness mean are the BS.1770-4 reference algorithm
// and are reproduced verbatim — see the formula references in the comment
// on `LoudnessAnalyzer::finalize()`.
//
// The playback side is `ReplayGain::apply()`: a single gain factor (from the
// tag or from a scanner) applied in linear to the planar float buffer.
// `kPreampDefaultDb` is the per-clipping-prevention headroom trim that the
// spec names in REQ-AUD-103; it is conservative because the integrated
// signal at -18 LUFS reference is not the same as a peak-bounded signal.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "audio/dsp/biquad.hpp"

namespace arrow::audio {

// ---------------------------------------------------------------------------
//  Loudness reference — REQ-AUD-100
// ---------------------------------------------------------------------------

/// Reference loudness, in LKFS (which is numerically the same as LUFS for
/// this calculation). REQ-AUD-100.
inline constexpr double kReplayGainReferenceLufs = -18.0;

/// REQ-AUD-103 — the pre-amp applied to the integrated signal to keep the
/// replay-gain application from clipping material that already lives close to
/// 0 dBFS. A scanner result of +6 dB ReplayGain on material at -3 dBFS peak
/// would otherwise land at +3 dBFS, which the limiter (REQ-AUD-110) can
/// absorb but which is better avoided in the first place.
inline constexpr double kPreampDefaultDb = -6.0;

/// REQ-AUD-103 — the bounded range of the pre-amp. ±15 dB.
inline constexpr double kRgPreampMinDb = -15.0;
inline constexpr double kRgPreampMaxDb = 15.0;

/// BS.1770-4 §3 — the absolute and relative gating thresholds, in LUFS.
inline constexpr double kAbsoluteGateLufs = -70.0;
inline constexpr double kRelativeGateOffsetDb = 10.0;

/// BS.1770-4 — gating block length and overlap, in seconds.
inline constexpr double kLoudnessBlockSeconds = 0.4;
inline constexpr double kLoudnessBlockOverlap = 0.75;  // 75 %

// ---------------------------------------------------------------------------
//  K-weighting filter — ITU-R BS.1770-4 Annex 1
// ---------------------------------------------------------------------------

/// The K-weighting filter: a first-order high-shelf (Stage 1, +4 dB at
/// 1681 Hz) followed by a high-pass RLB (Stage 2, ~38 Hz). The exact pole
/// and zero frequencies are the ones from BS.1770-4 Annex 1, and only those
/// values produce a BS.1770-4 measurement.
///
/// The two stages are exposed as separate biquad coefficients so the caller
/// can verify them against the standard or, if the use case allows, apply
/// only the RLB filter (e.g. when the K-weighting is being computed
/// off-line). For real-time use, use `KWeighting` below.
struct KWeightingCoeffs {
    BiquadCoeffs pre_filter;    ///< Stage 1, high-shelf +4 dB at 1681 Hz
    BiquadCoeffs rlb_filter;    ///< Stage 2, high-pass RLB at ~38 Hz

    /// Returns the BS.1770-4 reference coefficients for the named sample
    /// rate. The implementation is a no-op for sample rates that the
    /// standard does not cover.
    [[nodiscard]] static KWeightingCoeffs for_sample_rate(double fs) noexcept;
};

/// A two-stage K-weighting filter for a single channel. RT-SAFE after
/// construction. The `process_*` methods allocate nothing, lock nothing and
/// take no branches on external state.
class KWeighting {
  public:
    KWeighting() = default;

    /// Initialises both biquads with the BS.1770-4 reference coefficients
    /// for `fs`. A non-positive or non-finite `fs` leaves both sections
    /// as the identity (true no-op, REQ-AUD-005).
    void configure(double fs) noexcept;

    /// Resets the delay line. Call on seek and track change.
    void reset() noexcept;

    /// RT-SAFE: processes one sample.
    [[nodiscard]] float process_one(float in) noexcept {
        return static_cast<float>(rlb_.process_one(pre_.process_one(in)));
    }

    /// RT-SAFE: in-place block.
    void process_in_place(std::span<float> buffer) noexcept {
        pre_.process_in_place(buffer);
        rlb_.process_in_place(buffer);
    }

    [[nodiscard]] const Biquad& pre() const noexcept { return pre_; }
    [[nodiscard]] const Biquad& rlb() const noexcept { return rlb_; }

  private:
    Biquad pre_{};
    Biquad rlb_{};
};

// ---------------------------------------------------------------------------
//  Loudness analyzer — ITU-R BS.1770-4 §3
// ---------------------------------------------------------------------------

/// Channel weights for the BS.1770-4 loudness sum. The standard only gives
/// weights for the 5.1 layout: L=R=1.0, C=1.0, LFE=0.0, Ls=Rs=1.41. Mono
/// and stereo are special-cased: a single channel is treated as the left
/// front, two channels as L=R=1.0. Anything beyond 5.1 is treated as a 1.0
/// weight (we are not measuring surround bars, only the layout the spec
/// describes).
struct ChannelWeights {
    double left{1.0};
    double right{1.0};
    double center{1.0};
    double lfe{0.0};
    double left_surround{1.41};
    double right_surround{1.41};

    /// Returns the channel-weight table for `channels` channels following
    /// the BS.1770-4 convention.
    [[nodiscard]] static std::array<double, 6> for_channel_count(std::size_t channels) noexcept;
};

/// One block's worth of summed, K-weighted energy. The MS values are
/// per-channel; the loudness is the BS.1770-4 weighted sum.
struct LoudnessBlock {
    double mean_square[6]{};
    double loudness_lufs{-std::numeric_limits<double>::infinity()};
    bool above_absolute_gate{false};
    bool above_relative_gate{false};
};

/// Accumulates samples through K-weighting and reports integrated loudness
/// at the end. The intended use is the offline scanner described in
/// REQ-AUD-104: feed the entire file through `push_frames()`, then call
/// `finalize()` for the integrated result. The block-based gating mirrors
/// the BS.1770-4 reference algorithm step for step.
class LoudnessAnalyzer {
  public:
    /// Construct a new analyzer for `channels` channels at `fs` Hz.
    LoudnessAnalyzer(std::size_t channels, double fs);

    [[nodiscard]] std::size_t channels() const noexcept { return channels_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::size_t block_samples() const noexcept { return block_samples_; }
    [[nodiscard]] std::size_t hop_samples() const noexcept { return hop_samples_; }

    /// RT-SAFE: feeds one frame of `channels` planar samples. `planes`
    /// must be contiguous in the channels direction.
    void push_frame(const float* const* planes, std::size_t channels);

    /// Convenience: feeds a block of `frames` frames from a single sample
    /// buffer. `planes` follows the same shape as `PlanarFrames` from the
    /// audio ports module.
    void push_frames(const float* const* planes, std::size_t channels, std::size_t frames);

    /// Drains the partial block at the tail of the stream and computes the
    /// integrated loudness. RT-SAFE.
    void finalize();

    /// All complete blocks seen so far. The result is invalidated by any
    /// subsequent `push_frame()` / `push_frames()` / `finalize()` call.
    [[nodiscard]] const std::vector<LoudnessBlock>& blocks() const noexcept { return blocks_; }

    /// The integrated loudness, in LUFS. `-inf` if the stream contained
    /// no above-gate blocks (i.e. it was digital silence).
    [[nodiscard]] double integrated_lufs() const noexcept { return integrated_lufs_; }

    /// The per-channel sample-peak across the entire stream, in linear
    /// units (the user can convert to dBFS with 20*log10). 0.0 if no
    /// samples have been pushed.
    [[nodiscard]] double peak_linear() const noexcept { return peak_; }

    /// Resets the analyzer to a fresh state, keeping the channel and
    /// sample-rate configuration.
    void reset() noexcept;

  private:
    void process_pending_block(bool force);

    std::size_t channels_{0};
    double sample_rate_{48000.0};
    std::size_t block_samples_{0};
    std::size_t hop_samples_{0};

    std::vector<KWeighting> filters_;       ///< one per channel
    std::vector<double> weights_;            ///< per-channel BS.1770 weight
    std::vector<double> pending_ms_;         ///< per-channel MS accumulator for the current block
    std::size_t pending_count_{0};           ///< samples accumulated so far in the current block

    std::vector<LoudnessBlock> blocks_;
    double integrated_lufs_{-std::numeric_limits<double>::infinity()};
    double peak_{0.0};
};

// ---------------------------------------------------------------------------
//  ReplayGain value — the playback side
// ---------------------------------------------------------------------------

/// A complete ReplayGain analysis result.
struct ReplayGainResult {
    /// Track gain in dB, suitable for application at playback. NaN means
    /// "unmeasurable" (silent or too short to gate).
    double track_gain_db{std::numeric_limits<double>::quiet_NaN()};
    /// Album gain in dB. Equal to the track gain for single-track files.
    double album_gain_db{std::numeric_limits<double>::quiet_NaN()};
    /// The peak sample across all channels, in linear units (1.0 = 0 dBFS).
    double peak_linear{0.0};
    /// Integrated loudness in LUFS, or `-inf` if the stream was silent.
    double integrated_lufs{-std::numeric_limits<double>::infinity()};
};

/// A single playback gain setting combining a tag-derived gain and the
/// pre-amp. REQ-AUD-100 / REQ-AUD-102 / REQ-AUD-103.
struct ReplayGainSettings {
    enum class Mode { Off, Track, Album, Smart };
    Mode mode = Mode::Smart;

    /// Pre-amp in dB, applied before the track/album gain. REQ-AUD-102.
    double preamp_db = 0.0;
    /// Fallback gain (dB) for untagged tracks. REQ-AUD-102.
    double fallback_gain_db = 0.0;
    /// Track gain in dB, or NaN if untagged.
    double track_gain_db = std::numeric_limits<double>::quiet_NaN();
    /// Album gain in dB, or NaN if untagged.
    double album_gain_db = std::numeric_limits<double>::quiet_NaN();
    /// The peak that came in the tag, linear (1.0 = 0 dBFS). 0.0 if the
    /// tag was absent; an absent peak means the limiter (§8.9.8) is the
    /// only defence against clipping.
    double tag_peak_linear = 0.0;
    /// True when the album gain tag was present and is the one that should
    /// apply (i.e. Mode::Album or a Smart-mode album context).
    bool use_album_gain = false;

    /// True when the settings are unambiguously neutral.
    [[nodiscard]] bool is_neutral() const noexcept;
};

/// Computes the linear gain to apply to a buffer given the current settings.
/// Honours REQ-AUD-103 (peak-limited ReplayGain): if `tag_peak_linear *
/// gain_linear` would exceed 1.0, the gain is reduced so the result peaks
/// at exactly 1.0. Returns 1.0 in Off mode and 0.0 when settings are
/// otherwise degenerate.
[[nodiscard]] double replay_gain_linear(const ReplayGainSettings& s) noexcept;

/// Applies ReplayGain in place to a multi-channel planar float buffer. The
/// single gain factor (from `replay_gain_linear()`) is multiplied through
/// every sample; per-channel independence is not required because
/// ReplayGain is a single-factor loudness normalisation. RT-SAFE.
void apply_replay_gain(std::span<std::span<float>> planes, double gain_linear) noexcept;

// ---------------------------------------------------------------------------
//  Tag values — REQ-AUD-101
// ---------------------------------------------------------------------------

/// Parsed ReplayGain tags. Fields are NaN when the tag is absent.
struct ReplayGainTags {
    double track_gain_db{std::numeric_limits<double>::quiet_NaN()};
    double album_gain_db{std::numeric_limits<double>::quiet_NaN()};
    double track_peak_linear{0.0};
    double album_peak_linear{0.0};

    /// True when at least one of the track/album gain tags is present.
    [[nodiscard]] bool has_any() const noexcept {
        return std::isfinite(track_gain_db) || std::isfinite(album_gain_db);
    }
};

/// REQ-AUD-101 / REQ-AUD-103 — converts the raw tag string to dB. The
/// Vorbis/APE/ID3 convention is "−6.50 dB" (with the unit), the R128
/// convention is a Q7.8 fixed-point relative to −23 LUFS, and the iTunes
/// MP4 convention is a 16.16 fixed-point Q-number. NaN is returned for
/// unparseable input; the caller treats NaN as "tag absent".
[[nodiscard]] double parse_replaygain_db(std::string_view raw) noexcept;

/// REQ-AUD-101 / REQ-AUD-103 — parses a peak tag ("1.234567") to a
/// linear ratio. Returns 0.0 for unparseable input, matching the spec's
/// "absent" sentinel.
[[nodiscard]] double parse_replaygain_peak(std::string_view raw) noexcept;

/// Convenience: reads the standard tag set from a Vorbis/APE-style key/value
/// table and returns the combined result. The lookup follows the order
/// REQ-AUD-101 dictates: R128 first (Opus), then ReplayGain, then iTunes.
[[nodiscard]] ReplayGainTags replaygain_tags_from(
    std::string_view track_gain,
    std::string_view album_gain,
    std::string_view track_peak,
    std::string_view album_peak) noexcept;

}  // namespace arrow::audio
