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

  private:
    BiquadCoeffs c_{};
    // float64 state — see the file header for why.
    double x1_{0.0}, x2_{0.0}, y1_{0.0}, y2_{0.0};
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

}  // namespace arrow::audio
