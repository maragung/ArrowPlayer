// SPDX-License-Identifier: MPL-2.0
#include "audio/dsp/biquad.hpp"

#include <cmath>
#include <complex>
#include <numbers>

namespace arrow::audio {
namespace {

constexpr double kPi = std::numbers::pi;

/// True when the filter is a no-op for a gain-bearing type. Runtime-only:
/// `std::abs(double)` is not constexpr on MSVC (C3615), and this helper is
/// only ever called from `design()`.
bool negligible_gain(double gain_db) noexcept {
    return std::abs(gain_db) < 1e-9;
}

}  // namespace

double q_for_bandwidth_octaves(double octaves) noexcept {
    // REQ-AUD-083:  Q = sqrt(2^N) / (2^N - 1)
    if (!(octaves > 0.0)) return 1.0;
    const double p = std::pow(2.0, octaves);
    const double denom = p - 1.0;
    if (std::abs(denom) < 1e-12) return 1.0;
    return std::sqrt(p) / denom;
}

BiquadCoeffs design(
    FilterType type, double f0_hz, double sample_rate_hz, double q, double gain_db) noexcept {
    // Guard rails first. Any degenerate request yields a true identity so the
    // caller can skip the section entirely (REQ-AUD-005).
    if (!(sample_rate_hz > 0.0) || !(f0_hz > 0.0) || !(q > 0.0)) {
        return BiquadCoeffs::identity();
    }
    if (!std::isfinite(f0_hz) || !std::isfinite(q) || !std::isfinite(gain_db)) {
        return BiquadCoeffs::identity();
    }

    // REQ-AUD-084: bands too close to Nyquist are bypassed, not clamped.
    const double nyquist = sample_rate_hz * 0.5;
    if (f0_hz >= nyquist * kMaxNyquistFraction) {
        return BiquadCoeffs::identity();
    }

    // Gain-bearing types at 0 dB are exact no-ops.
    const bool gain_bearing = (type == FilterType::Peaking || type == FilterType::LowShelf ||
                               type == FilterType::HighShelf);
    if (gain_bearing && negligible_gain(gain_db)) {
        return BiquadCoeffs::identity();
    }

    const double w0 = 2.0 * kPi * f0_hz / sample_rate_hz;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
        case FilterType::Peaking: {
            // REQ-AUD-082, verbatim from the spec:
            //   A = 10^(dBgain/40)
            //   b0 = 1 + alpha*A ; b1 = -2cos(w0) ; b2 = 1 - alpha*A
            //   a0 = 1 + alpha/A ; a1 = -2cos(w0) ; a2 = 1 - alpha/A
            const double A = std::pow(10.0, gain_db / 40.0);
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosw0;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha / A;
            break;
        }
        case FilterType::LowShelf: {
            const double A = std::pow(10.0, gain_db / 40.0);
            const double sqrtA = std::sqrt(A);
            const double two_sqrtA_alpha = 2.0 * sqrtA * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + two_sqrtA_alpha);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - two_sqrtA_alpha);
            a0 = (A + 1.0) + (A - 1.0) * cosw0 + two_sqrtA_alpha;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
            a2 = (A + 1.0) + (A - 1.0) * cosw0 - two_sqrtA_alpha;
            break;
        }
        case FilterType::HighShelf: {
            const double A = std::pow(10.0, gain_db / 40.0);
            const double sqrtA = std::sqrt(A);
            const double two_sqrtA_alpha = 2.0 * sqrtA * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + two_sqrtA_alpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - two_sqrtA_alpha);
            a0 = (A + 1.0) - (A - 1.0) * cosw0 + two_sqrtA_alpha;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
            a2 = (A + 1.0) - (A - 1.0) * cosw0 - two_sqrtA_alpha;
            break;
        }
        case FilterType::LowPass: {
            b0 = (1.0 - cosw0) * 0.5;
            b1 = 1.0 - cosw0;
            b2 = (1.0 - cosw0) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha;
            break;
        }
        case FilterType::HighPass: {
            b0 = (1.0 + cosw0) * 0.5;
            b1 = -(1.0 + cosw0);
            b2 = (1.0 + cosw0) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha;
            break;
        }
        case FilterType::BandPass: {
            // Constant 0 dB peak gain.
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha;
            break;
        }
        case FilterType::Notch: {
            b0 = 1.0;
            b1 = -2.0 * cosw0;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha;
            break;
        }
        case FilterType::AllPass: {
            b0 = 1.0 - alpha;
            b1 = -2.0 * cosw0;
            b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosw0;
            a2 = 1.0 - alpha;
            break;
        }
    }

    if (!std::isfinite(a0) || std::abs(a0) < 1e-30) {
        return BiquadCoeffs::identity();
    }

    BiquadCoeffs c;
    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;

    if (!std::isfinite(c.b0) || !std::isfinite(c.b1) || !std::isfinite(c.b2) ||
        !std::isfinite(c.a1) || !std::isfinite(c.a2)) {
        return BiquadCoeffs::identity();
    }
    return c;
}

double magnitude_db(const BiquadCoeffs& c, double freq_hz, double sample_rate_hz) noexcept {
    if (!(sample_rate_hz > 0.0) || freq_hz < 0.0) return 0.0;

    const double w = 2.0 * kPi * freq_hz / sample_rate_hz;
    const std::complex<double> z1{std::cos(-w), std::sin(-w)};  // z^-1
    const std::complex<double> z2 = z1 * z1;                    // z^-2

    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;

    const double dmag = std::abs(den);
    if (dmag < 1e-30) return 0.0;

    const double mag = std::abs(num) / dmag;
    if (mag < 1e-12) return -240.0;  // floor, avoids -inf in plots
    return 20.0 * std::log10(mag);
}

void Biquad::process_in_place(std::span<float> buffer) noexcept {
    for (float& s : buffer) s = process_one(s);
}

void Biquad::process(std::span<const float> in, std::span<float> out) noexcept {
    const std::size_t n = in.size() < out.size() ? in.size() : out.size();
    for (std::size_t i = 0; i < n; ++i) out[i] = process_one(in[i]);
}

// ---------------------------------------------------------------------------
//  Cascade
// ---------------------------------------------------------------------------

void BiquadCascade::resize(std::size_t sections) {
    sections_.assign(sections, Biquad{});
}

void BiquadCascade::process_in_place(std::span<float> buffer) noexcept {
    for (auto& section : sections_) {
        // REQ-AUD-005: a bypassed band is skipped, not run at unity.
        if (section.coeffs().is_identity()) continue;
        section.process_in_place(buffer);
    }
}

double BiquadCascade::magnitude_db(double freq_hz, double sample_rate_hz) const noexcept {
    // Cascaded sections multiply in linear magnitude, so they add in dB.
    double total = 0.0;
    for (const auto& section : sections_) {
        if (section.coeffs().is_identity()) continue;
        total += audio::magnitude_db(section.coeffs(), freq_hz, sample_rate_hz);
    }
    return total;
}

// ---------------------------------------------------------------------------
//  CrossfadingCascade
// ---------------------------------------------------------------------------

void CrossfadingCascade::resize(std::size_t sections) {
    sections_.assign(sections, CrossfadingBiquad{});
}

// ---------------------------------------------------------------------------
//  Filter factories — REQ-AUD-082
// ---------------------------------------------------------------------------

Biquad make_peaking(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::Peaking, f0, fs, q, gain_db)};
}

Biquad make_low_shelf(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::LowShelf, f0, fs, q, gain_db)};
}

Biquad make_high_shelf(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::HighShelf, f0, fs, q, gain_db)};
}

Biquad make_low_pass(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::LowPass, f0, fs, q, gain_db)};
}

Biquad make_high_pass(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::HighPass, f0, fs, q, gain_db)};
}

Biquad make_band_pass(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::BandPass, f0, fs, q, gain_db)};
}

Biquad make_notch(double fs, double f0, double q, double gain_db) noexcept {
    return Biquad{design(FilterType::Notch, f0, fs, q, gain_db)};
}

}  // namespace arrow::audio
