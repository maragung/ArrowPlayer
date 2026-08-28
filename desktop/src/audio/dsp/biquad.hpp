// SPDX-License-Identifier: MPL-2.0
// Biquad filters — spec §8.9.1 (REQ-AUD-082, REQ-AUD-083, REQ-AUD-086).
//
// Coefficients follow the Robert Bristow-Johnson (RBJ) Audio EQ Cookbook, which
// is what the spec mandates. State is float64 even though the signal path is
// float32 (REQ-AUD-082): float32 state accumulates audible error in
// low-frequency, high-Q sections, and the cost of double state is negligible
// next to the memory traffic of the sample buffers themselves.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace arrow::audio {

/// Filter topologies. All use the RBJ formulas for the named response.
enum class FilterType {
    Peaking,    ///< the graphic-EQ workhorse
    LowShelf,   ///< bass boost (REQ-AUD-095)
    HighShelf,  ///< treble boost
    LowPass,
    HighPass,
    BandPass,  ///< constant 0 dB peak gain
    Notch,
    AllPass,
};

/// Normalised transfer-function coefficients for
///   H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)
/// a0 has already been divided out.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;

    /// The identity filter: output == input, bit-exactly.
    [[nodiscard]] static constexpr BiquadCoeffs identity() noexcept { return {}; }

    /// True when this is the identity, so callers can skip the section entirely.
    /// REQ-AUD-005 requires bypass to be a *true* bypass, not a 0 dB run.
    [[nodiscard]] bool is_identity() const noexcept {
        return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0;
    }
};

/// The maximum fraction of Nyquist at which we will place a centre frequency.
/// REQ-AUD-084: bands above this are bypassed rather than clamped, because the
/// bilinear-transform coefficients become numerically unstable near Nyquist.
inline constexpr double kMaxNyquistFraction = 0.95;

/// Q derived from bandwidth in octaves — REQ-AUD-083:
///     Q = sqrt(2^N) / (2^N - 1)
/// N = 1.0 (octave, 10-band)      -> 1.4142
/// N = 0.5 (half-octave, 18-band) -> 2.8710
[[nodiscard]] double q_for_bandwidth_octaves(double octaves) noexcept;

/// Designs coefficients. Returns `identity()` when the filter would be a no-op
/// or when f0 exceeds `kMaxNyquistFraction * sample_rate/2` (REQ-AUD-084).
///
/// `gain_db` is used by Peaking, LowShelf and HighShelf; it is ignored by the
/// other types. `q` must be > 0; non-positive values yield identity.
[[nodiscard]] BiquadCoeffs design(
    FilterType type, double f0_hz, double sample_rate_hz, double q, double gain_db) noexcept;

/// Evaluates |H(e^{jw})| in dB at `freq_hz`. Used by REQ-AUD-088 to draw the
/// real computed frequency response rather than a cosmetic spline, and by the
/// §8.11 test 6 verification that measured response matches the analytic one.
[[nodiscard]] double magnitude_db(const BiquadCoeffs& c,
                                  double freq_hz,
                                  double sample_rate_hz) noexcept;

// ---------------------------------------------------------------------------
//  Filter factories — REQ-AUD-082
//
//  These are thin convenience wrappers around `design()`. They exist so that
//  the call site reads as the filter the caller actually wants, rather than as
//  a four-argument bag of numbers. Each factory returns a Biquad that is
//  already populated with the designed coefficients and is therefore ready
//  for the audio thread to use without further configuration. The Biquad
//  itself supplies `process_one(float)` and the block variants
//  `process_in_place(std::span<float>)` and `process(float*, size_t)`, all of
//  which are RT-safe. Bypass is a true no-op via `BiquadCoeffs::identity()`:
//  the cascade machinery in `BiquadCascade` and `CrossfadingCascade` skips
//  identity sections outright (REQ-AUD-005).
// ---------------------------------------------------------------------------

class Biquad;

/// REQ-AUD-082 / REQ-AUD-095 — peaking EQ biquad. The graphic-EQ workhorse.
[[nodiscard]] Biquad make_peaking(double fs, double f0, double q, double gain_db) noexcept;

/// REQ-AUD-082 — low-shelf EQ biquad. `gain_db` is the gain in the passband.
[[nodiscard]] Biquad make_low_shelf(double fs, double f0, double q, double gain_db) noexcept;

/// REQ-AUD-082 — high-shelf EQ biquad. `gain_db` is the gain in the passband.
[[nodiscard]] Biquad make_high_shelf(double fs, double f0, double q, double gain_db) noexcept;

/// REQ-AUD-082 — second-order low-pass with the RBJ constant-0-dB-peak
/// topology. `gain_db` is unused; the parameter exists for symmetry with the
/// other factories.
[[nodiscard]] Biquad make_low_pass(double fs, double f0, double q, double gain_db = 0.0) noexcept;

/// REQ-AUD-082 — second-order high-pass with the RBJ constant-0-dB-peak
/// topology. `gain_db` is unused.
[[nodiscard]] Biquad make_high_pass(double fs, double f0, double q, double gain_db = 0.0) noexcept;

/// REQ-AUD-082 — constant-0-dB-peak band-pass. `gain_db` is unused.
[[nodiscard]] Biquad make_band_pass(double fs, double f0, double q, double gain_db = 0.0) noexcept;

/// REQ-AUD-082 — notch (band-stop). `gain_db` is unused.
[[nodiscard]] Biquad make_notch(double fs, double f0, double q, double gain_db = 0.0) noexcept;

/// A single Direct Form I biquad section for one channel.
///
/// Direct Form I is chosen over DF-II/transposed forms because its state is the
/// raw input/output history, which makes coefficient cross-fading (REQ-AUD-085)
/// well-behaved: the state stays meaningful when coefficients change.
///
/// RT-SAFE: process() and process_in_place() allocate nothing, lock nothing and
/// take no branches on external state.
class Biquad {
  public:
    Biquad() = default;

    explicit Biquad(const BiquadCoeffs& c) noexcept : c_{c} {}

    void set_coeffs(const BiquadCoeffs& c) noexcept { c_ = c; }

    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return c_; }

    /// Clears the delay line. Call on seek/track change to avoid dragging the
    /// previous track's tail across a boundary.
    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = 0.0; }

    /// RT-SAFE: processes one sample.
    [[nodiscard]] float process_one(float in) noexcept {
        const double x = static_cast<double>(in);
        const double y = c_.b0 * x + c_.b1 * x1_ + c_.b2 * x2_ - c_.a1 * y1_ - c_.a2 * y2_;
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = y;
        return static_cast<float>(y);
    }

    /// RT-SAFE: processes a block in place.
    void process_in_place(std::span<float> buffer) noexcept;

    /// RT-SAFE: processes `in` into `out`; sizes must match.
    void process(std::span<const float> in, std::span<float> out) noexcept;

    /// Convenience overload matching the `process(float*, size_t)` shape the
    /// spec calls out. `in == out` is allowed (in-place). `n` is the sample
    /// count; both buffers must hold at least `n` samples. RT-SAFE.
    void process(const float* in, float* out, std::size_t n) noexcept {
        if (in == nullptr || out == nullptr) return;
        process(std::span<const float>{in, n}, std::span<float>{out, n});
    }

    /// True-bypass block overload: leaves the buffer exactly as it was found
    /// (REQ-AUD-005). Provided as a peer to `process(float*, float*, size_t)`
    /// so the caller can switch between bypass and active on the same buffer.
    static void process_bypass(float* buffer, std::size_t n) noexcept {
        (void)buffer;
        (void)n;
        // Intentionally a no-op: REQ-AUD-005 says bypass MUST be a true bypass,
        // not a 0 dB run. Touching the buffer here would be extra work for
        // nothing and would break the §8.11 bit-exact-bypass test.
    }

  private:
    BiquadCoeffs c_{};
    // float64 state — see the file header for why.
    double x1_{0.0}, x2_{0.0}, y1_{0.0}, y2_{0.0};
};

/// A biquad that holds *two* coefficient sets and cross-fades between them
/// (REQ-AUD-085). While a ramp is in progress, every output sample is a linear
/// mix of the two parallel Direct-Form-I biquads running on independent state;
/// the mix position advances by `1 / ramp_length` per sample. Once the ramp
/// reaches the end, the new set is the only one running and the old one is
/// discarded on the next `set_coeffs()` call.
///
/// The cross-fade is what makes a 32 ms gain change audibly transparent: an
/// instantaneous coefficient swap produces a step in the impulse response
/// that the ear reads as a click. A linear mix is the cheapest smoother that
/// still passes the §8.11 "no zipper noise" test.
///
/// RT-SAFE: the `process_*` methods allocate nothing, lock nothing and read
/// only the locally-stored state.
class CrossfadingBiquad {
  public:
    CrossfadingBiquad() = default;

    explicit CrossfadingBiquad(const BiquadCoeffs& c) noexcept {
        current_.set_coeffs(c);
        target_.set_coeffs(c);
    }

    /// The currently-active coefficient set. While ramping, this is the
    /// "from" slot of the cross-fade; once the ramp is finished it mirrors
    /// `target_`.
    [[nodiscard]] const BiquadCoeffs& coeffs() const noexcept { return current_.coeffs(); }

    /// Sets both coefficient slots at once. The cascade processes audio with
    /// the new coefficients from the next sample without any gap. The
    /// intended pattern is then to call `set_target_coeffs()` again to mark
    /// what to *next* fade towards, and `start_ramp()` to perform the
    /// cross-fade (REQ-AUD-085).
    void set_coeffs(const BiquadCoeffs& c) noexcept {
        current_.set_coeffs(c);
        target_.set_coeffs(c);
        ramp_remaining_ = 0;
    }

    /// Sets only the upcoming-ramp target without disturbing the running set.
    /// Use this from the UI thread, then call `start_ramp()` to actually
    /// perform the cross-fade from the audio thread (REQ-AUD-085).
    void set_target_coeffs(const BiquadCoeffs& c) noexcept {
        target_.set_coeffs(c);
    }

    /// Begins a cross-fade from the current value to the most recent target.
    /// `ramp_samples` is the total number of samples over which the fade
    /// happens. 0 or 1 means an instant snap; values <= 0 disable the ramp.
    void start_ramp(std::size_t ramp_samples) noexcept {
        if (ramp_samples <= 1) {
            // No fade at all: collapse the ramp, keep only the target.
            current_.set_coeffs(target_.coeffs());
            current_.reset();
            target_.reset();
            ramp_remaining_ = 0;
            return;
        }
        // Seed the fade-in biquad with the new coefficients but keep its state
        // empty; the cross-fade mix supplies the continuity.
        target_.reset();
        ramp_remaining_ = ramp_samples;
        ramp_total_ = ramp_samples;
    }

    [[nodiscard]] bool ramping() const noexcept { return ramp_remaining_ > 0; }

    [[nodiscard]] std::size_t ramp_remaining() const noexcept { return ramp_remaining_; }

    /// Resets both sets of state. Does not touch the coefficient slots.
    void reset_state() noexcept {
        current_.reset();
        target_.reset();
    }

    [[nodiscard]] float process_one(float in) noexcept {
        if (!ramping()) {
            return current_.process_one(in);
        }
        // Compute both biquad outputs in double precision and mix in double,
        // so the linear cross-fade doesn't accumulate float-rounding error on
        // the 32 ms ramp.
        const double a = current_.process_one(static_cast<double>(in));
        const double b = target_.process_one(static_cast<double>(in));
        // Linear mix: weight = remaining / total so the new biquad's
        // contribution grows from ~0 to 1 across the ramp.
        const double w_new = 1.0 -
            (static_cast<double>(ramp_remaining_) / static_cast<double>(ramp_total_));
        const double out = a * (1.0 - w_new) + b * w_new;
        if (ramp_remaining_ > 0) --ramp_remaining_;
        if (ramp_remaining_ == 0) {
            // Ramp finished: the new set takes over and we drop the old one.
            current_.set_coeffs(target_.coeffs());
            // Note: we keep current_'s state as-is; it is the running state of
            // the biquad that has been processing the entire time.
        }
        return static_cast<float>(out);
    }

    void process_in_place(std::span<float> buffer) noexcept {
        for (float& s : buffer) s = process_one(s);
    }

    void process(std::span<const float> in, std::span<float> out) noexcept {
        const std::size_t n = in.size() < out.size() ? in.size() : out.size();
        for (std::size_t i = 0; i < n; ++i) out[i] = process_one(in[i]);
    }

  private:
    Biquad current_{};
    Biquad target_{};
    std::size_t ramp_remaining_{0};
    std::size_t ramp_total_{1};
};

/// A cascade of biquad sections for a single channel.
///
/// RT-SAFE once `resize()` has been called: processing never allocates.
class BiquadCascade {
  public:
    /// Allocates section storage. NOT RT-safe — call from the UI thread.
    void resize(std::size_t sections);

    [[nodiscard]] std::size_t size() const noexcept { return sections_.size(); }

    void set_coeffs(std::size_t index, const BiquadCoeffs& c) noexcept {
        if (index < sections_.size()) sections_[index].set_coeffs(c);
    }

    /// True when `index` names a real section.
    [[nodiscard]] bool has_section(std::size_t index) const noexcept {
        return index < sections_.size();
    }

    /// Coefficients of section `index`, or the identity when out of range.
    /// Returned by value: a pointer-returning accessor forces every caller to
    /// prove non-null, which the optimiser cannot always see through, and the
    /// struct is only five doubles.
    [[nodiscard]] BiquadCoeffs coeffs(std::size_t index) const noexcept {
        return index < sections_.size() ? sections_[index].coeffs() : BiquadCoeffs::identity();
    }

    void reset() noexcept {
        for (auto& s : sections_) s.reset();
    }

    /// RT-SAFE: runs every non-identity section in order. Identity sections are
    /// skipped outright (REQ-AUD-005).
    void process_in_place(std::span<float> buffer) noexcept;

    /// Combined magnitude response of the whole cascade, in dB.
    [[nodiscard]] double magnitude_db(double freq_hz, double sample_rate_hz) const noexcept;

  private:
    std::vector<Biquad> sections_;
};

/// A crossfading cascade. Behaves like `BiquadCascade` for the purposes of
/// magnitude-response inspection, but every section is a `CrossfadingBiquad`
/// that is mid-fade whenever `ramping()` is true. The intended use is the
/// equalizer: the UI thread calls `set_target_coeffs()` for each band, then
/// `start_ramp(ramp_samples)`, and the audio thread runs the fades
/// transparently in `process_in_place()` (REQ-AUD-085).
///
/// RT-SAFE once `resize()` has been called: processing never allocates.
class CrossfadingCascade {
  public:
    void resize(std::size_t sections);

    [[nodiscard]] std::size_t size() const noexcept { return sections_.size(); }

    /// Sets both the current and target coefficient slots. Use this for the
    /// initial fill (no fade required) and for instant snaps.
    void set_coeffs(std::size_t index, const BiquadCoeffs& c) noexcept {
        if (index < sections_.size()) sections_[index].set_coeffs(c);
    }

    /// Sets only the target slot. Use this from the UI thread together with
    /// `start_ramp()` to publish a change for the audio thread to cross-fade
    /// into (REQ-AUD-085).
    void set_target_coeffs(std::size_t index, const BiquadCoeffs& c) noexcept {
        if (index < sections_.size()) sections_[index].set_target_coeffs(c);
    }

    [[nodiscard]] BiquadCoeffs coeffs(std::size_t index) const noexcept {
        return index < sections_.size() ? sections_[index].coeffs() : BiquadCoeffs::identity();
    }

    [[nodiscard]] bool has_section(std::size_t index) const noexcept {
        return index < sections_.size();
    }

    [[nodiscard]] bool ramping() const noexcept {
        for (const auto& s : sections_) {
            if (s.ramping()) return true;
        }
        return false;
    }

    /// The largest per-section remaining-ramp count. The UI can poll this to
    /// avoid stacking a second ramp on top of one already in flight.
    [[nodiscard]] std::size_t ramp_remaining() const noexcept {
        std::size_t best = 0;
        for (const auto& s : sections_) {
            best = std::max(best, s.ramp_remaining());
        }
        return best;
    }

    void start_ramp(std::size_t ramp_samples) noexcept {
        for (auto& s : sections_) s.start_ramp(ramp_samples);
    }

    void reset() noexcept {
        for (auto& s : sections_) s.reset_state();
    }

    void process_in_place(std::span<float> buffer) noexcept {
        for (auto& section : sections_) {
            if (section.coeffs().is_identity()) continue;
            section.process_in_place(buffer);
        }
    }

    [[nodiscard]] double magnitude_db(double freq_hz, double sample_rate_hz) const noexcept {
        double total = 0.0;
        for (const auto& s : sections_) {
            if (s.coeffs().is_identity()) continue;
            total += audio::magnitude_db(s.coeffs(), freq_hz, sample_rate_hz);
        }
        return total;
    }

  private:
    std::vector<CrossfadingBiquad> sections_;
};

}  // namespace arrow::audio
