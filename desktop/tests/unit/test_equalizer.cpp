// SPDX-License-Identifier: MPL-2.0
// Tests for the biquad/EQ layer — spec §8.9.1 and the §8.11 verification table.
//
// The important tests here are the ones that pin *audio* correctness rather than
// code coverage:
//   * Q derived from bandwidth matches the spec's stated values exactly
//   * a 0 dB band is a TRUE identity, not a unity-gain run   (REQ-AUD-005)
//   * bands above 0.95*Nyquist are bypassed, not clamped     (REQ-AUD-084)
//   * the analytic response matches the response measured by running an
//     impulse through the filter and taking a DFT              (§8.11 test 6)
//   * a fully bypassed chain returns bit-identical samples      (§8.11 test 2)

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "audio/dsp/biquad.hpp"
#include "audio/dsp/equalizer.hpp"

#include <gtest/gtest.h>

using namespace arrow;
using namespace arrow::audio;

namespace {

constexpr double kPi = std::numbers::pi;

/// Measures |H(f)| in dB empirically: feed a unit impulse, capture the impulse
/// response, then evaluate its DFT at `freq_hz`. This is an independent path to
/// the same answer as magnitude_db(), so agreement is a real cross-check rather
/// than a tautology.
double measured_magnitude_db(BiquadCoeffs c, double freq_hz, double fs, std::size_t n = 32768) {
    Biquad f{c};
    std::vector<float> h(n, 0.0f);
    h[0] = 1.0f;
    f.process_in_place(h);

    const double w = 2.0 * kPi * freq_hz / fs;
    std::complex<double> acc{0.0, 0.0};
    for (std::size_t k = 0; k < n; ++k) {
        const double angle = -w * static_cast<double>(k);
        acc +=
            static_cast<double>(h[k]) * std::complex<double>{std::cos(angle), std::sin(angle)};
    }
    const double mag = std::abs(acc);
    return mag < 1e-12 ? -240.0 : 20.0 * std::log10(mag);
}

EqSettings graphic_settings(EqMode mode, std::vector<double> gains, double preamp = 0.0) {
    EqSettings s;
    s.enabled = true;
    s.mode = mode;
    s.preamp_db = preamp;
    s.graphic_gains_db = std::move(gains);
    return s;
}

}  // namespace

// ===========================================================================
//  Q derivation — REQ-AUD-083
// ===========================================================================

TEST(BiquadQ, MatchesSpecifiedValues) {
    // The spec states these two values explicitly; they are load-bearing because
    // the Android implementation must derive the identical numbers.
    EXPECT_NEAR(q_for_bandwidth_octaves(1.0), 1.4142, 0.0001);
    EXPECT_NEAR(q_for_bandwidth_octaves(0.5), 2.8710, 0.0001);
}

TEST(BiquadQ, FormulaIsSqrt2PowNOverPowNMinus1) {
    for (const double n : {0.25, 0.5, 1.0, 1.5, 2.0, 3.0}) {
        const double p = std::pow(2.0, n);
        EXPECT_NEAR(q_for_bandwidth_octaves(n), std::sqrt(p) / (p - 1.0), 1e-12);
    }
}

TEST(BiquadQ, RejectsNonPositiveBandwidth) {
    EXPECT_GT(q_for_bandwidth_octaves(0.0), 0.0);
    EXPECT_GT(q_for_bandwidth_octaves(-1.0), 0.0);
}

TEST(BiquadQ, ModeBandwidthMapping) {
    EXPECT_DOUBLE_EQ(bandwidth_octaves_for_mode(EqMode::Graphic10), 1.0);
    EXPECT_DOUBLE_EQ(bandwidth_octaves_for_mode(EqMode::Graphic18), 0.5);
}

// ===========================================================================
//  Band tables — REQ-AUD-080
// ===========================================================================

TEST(EqBands, TenBandTableIsIsoOctave) {
    ASSERT_EQ(kBands10.size(), 10u);
    EXPECT_DOUBLE_EQ(kBands10.front(), 31.5);
    EXPECT_DOUBLE_EQ(kBands10.back(), 16000.0);
    // Each band is an octave above the previous (ratio 2), within rounding.
    for (std::size_t i = 1; i < kBands10.size(); ++i) {
        EXPECT_NEAR(kBands10[i] / kBands10[i - 1], 2.0, 0.02) << "band " << i;
    }
}

TEST(EqBands, EighteenBandTableIsHalfOctave) {
    ASSERT_EQ(kBands18.size(), 18u);
    EXPECT_DOUBLE_EQ(kBands18.front(), 31.5);
    EXPECT_DOUBLE_EQ(kBands18.back(), 11314.0);
    // Ratio sqrt(2) ~= 1.414 between neighbours.
    for (std::size_t i = 1; i < kBands18.size(); ++i) {
        EXPECT_NEAR(kBands18[i] / kBands18[i - 1], std::sqrt(2.0), 0.02) << "band " << i;
    }
}

TEST(EqBands, TablesAreStrictlyAscending) {
    EXPECT_TRUE(std::is_sorted(kBands10.begin(), kBands10.end()));
    EXPECT_TRUE(std::is_sorted(kBands18.begin(), kBands18.end()));
}

// ===========================================================================
//  True bypass — REQ-AUD-005
// ===========================================================================

TEST(BiquadBypass, ZeroDbPeakingIsExactIdentity) {
    const auto c = design(FilterType::Peaking, 1000.0, 48000.0, 1.41, 0.0);
    EXPECT_TRUE(c.is_identity());
}

TEST(BiquadBypass, IdentityPassesSamplesBitExactly) {
    // §8.11 test 2 in miniature: bypass must be bit-identical, not merely close.
    Biquad f{BiquadCoeffs::identity()};
    std::vector<float> in;
    for (int i = 0; i < 4096; ++i) {
        in.push_back(static_cast<float>(std::sin(static_cast<double>(i) * 0.05) * 0.9));
    }
    const std::vector<float> original = in;
    f.process_in_place(in);
    for (std::size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(in[i]),
                  std::bit_cast<std::uint32_t>(original[i]))
            << "sample " << i;
    }
}

TEST(BiquadBypass, DegenerateParametersYieldIdentity) {
    EXPECT_TRUE(design(FilterType::Peaking, 1000.0, 0.0, 1.0, 6.0).is_identity());
    EXPECT_TRUE(design(FilterType::Peaking, 0.0, 48000.0, 1.0, 6.0).is_identity());
    EXPECT_TRUE(design(FilterType::Peaking, 1000.0, 48000.0, 0.0, 6.0).is_identity());
    EXPECT_TRUE(design(FilterType::Peaking, 1000.0, 48000.0, -1.0, 6.0).is_identity());
    const double nan = std::nan("");
    EXPECT_TRUE(design(FilterType::Peaking, nan, 48000.0, 1.0, 6.0).is_identity());
    EXPECT_TRUE(design(FilterType::Peaking, 1000.0, 48000.0, 1.0, nan).is_identity());
}

// ===========================================================================
//  Nyquist guard — REQ-AUD-084
// ===========================================================================

TEST(BiquadNyquist, BandsAboveNinetyFivePercentAreBypassed) {
    // At 44.1 kHz the 16 kHz band is valid (0.95*22050 = 20947).
    EXPECT_FALSE(design(FilterType::Peaking, 16000.0, 44100.0, 1.41, 6.0).is_identity());
    // At 22.05 kHz it is not (0.95*11025 = 10474).
    EXPECT_TRUE(design(FilterType::Peaking, 16000.0, 22050.0, 1.41, 6.0).is_identity());
}

TEST(BiquadNyquist, BoundaryIsExactlyNinetyFivePercent) {
    const double fs = 48000.0;
    const double limit = fs * 0.5 * kMaxNyquistFraction;  // 22800
    EXPECT_TRUE(design(FilterType::Peaking, limit + 1.0, fs, 1.0, 6.0).is_identity());
    EXPECT_FALSE(design(FilterType::Peaking, limit - 1.0, fs, 1.0, 6.0).is_identity());
}

TEST(BiquadNyquist, HighBandStaysStableAtLowSampleRates) {
    // The point of bypassing rather than clamping: no NaN/Inf coefficients.
    for (const double fs : {8000.0, 11025.0, 16000.0, 22050.0, 44100.0, 192000.0}) {
        for (const double f : kBands18) {
            const auto c = design(FilterType::Peaking, f, fs, 2.871, 12.0);
            EXPECT_TRUE(std::isfinite(c.b0)) << "fs=" << fs << " f=" << f;
            EXPECT_TRUE(std::isfinite(c.b1));
            EXPECT_TRUE(std::isfinite(c.b2));
            EXPECT_TRUE(std::isfinite(c.a1));
            EXPECT_TRUE(std::isfinite(c.a2));
        }
    }
}

// ===========================================================================
//  Analytic vs measured response — §8.11 test 6
// ===========================================================================

TEST(BiquadResponse, AnalyticMatchesMeasuredAtCentreFrequency) {
    const double fs = 48000.0;
    for (const double gain : {-12.0, -6.0, -3.0, 3.0, 6.0, 12.0}) {
        const double f0 = 1000.0;
        const auto c = design(FilterType::Peaking, f0, fs, 1.4142, gain);
        const double analytic = magnitude_db(c, f0, fs);
        const double measured = measured_magnitude_db(c, f0, fs);
        // A peaking filter's gain at its centre frequency is exactly `gain`.
        EXPECT_NEAR(analytic, gain, 0.05) << "gain=" << gain;
        EXPECT_NEAR(analytic, measured, 0.25) << "gain=" << gain;
    }
}

TEST(BiquadResponse, AnalyticMatchesMeasuredAcrossSpectrum) {
    // The spec's acceptance criterion is +/-0.25 dB across 20 Hz - 20 kHz.
    const double fs = 48000.0;
    const auto c = design(FilterType::Peaking, 1000.0, fs, 1.4142, 9.0);
    for (const double f :
         {20.0, 50.0, 100.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 16000.0, 20000.0}) {
        const double analytic = magnitude_db(c, f, fs);
        const double measured = measured_magnitude_db(c, f, fs);
        EXPECT_NEAR(analytic, measured, 0.25) << "f=" << f;
    }
}

TEST(BiquadResponse, PeakingIsUnityFarFromCentre) {
    const double fs = 48000.0;
    const auto c = design(FilterType::Peaking, 1000.0, fs, 4.0, 12.0);
    // Two decades below and (nearly) one above, a high-Q bell is back to ~0 dB.
    EXPECT_NEAR(magnitude_db(c, 20.0, fs), 0.0, 0.5);
    EXPECT_NEAR(magnitude_db(c, 18000.0, fs), 0.0, 0.5);
}

TEST(BiquadResponse, ShelvesReachTargetGainInTheirBand) {
    const double fs = 48000.0;

    const auto low = design(FilterType::LowShelf, 200.0, fs, 0.707, 8.0);
    EXPECT_NEAR(magnitude_db(low, 20.0, fs), 8.0, 0.6);    // deep in the shelf
    EXPECT_NEAR(magnitude_db(low, 8000.0, fs), 0.0, 0.3);  // above it

    const auto high = design(FilterType::HighShelf, 4000.0, fs, 0.707, 8.0);
    EXPECT_NEAR(magnitude_db(high, 20000.0, fs), 8.0, 0.6);
    EXPECT_NEAR(magnitude_db(high, 100.0, fs), 0.0, 0.3);
}

TEST(BiquadResponse, LowPassAndHighPassAreMinusThreeDbAtCutoff) {
    const double fs = 48000.0;
    const double fc = 1000.0;
    const auto lp = design(FilterType::LowPass, fc, fs, 0.7071, 0.0);
    const auto hp = design(FilterType::HighPass, fc, fs, 0.7071, 0.0);
    EXPECT_NEAR(magnitude_db(lp, fc, fs), -3.0, 0.2);
    EXPECT_NEAR(magnitude_db(hp, fc, fs), -3.0, 0.2);
    // And they roll off in the right directions.
    EXPECT_LT(magnitude_db(lp, 8000.0, fs), -18.0);
    EXPECT_LT(magnitude_db(hp, 125.0, fs), -18.0);
}

TEST(BiquadResponse, NotchRejectsAtCentre) {
    const double fs = 48000.0;
    const auto n = design(FilterType::Notch, 1000.0, fs, 8.0, 0.0);
    EXPECT_LT(magnitude_db(n, 1000.0, fs), -40.0);
    EXPECT_NEAR(magnitude_db(n, 100.0, fs), 0.0, 0.4);
}

TEST(BiquadResponse, BandPassPeaksAtUnity) {
    const double fs = 48000.0;
    const auto b = design(FilterType::BandPass, 1000.0, fs, 2.0, 0.0);
    EXPECT_NEAR(magnitude_db(b, 1000.0, fs), 0.0, 0.2);
    EXPECT_LT(magnitude_db(b, 100.0, fs), -12.0);
}

TEST(BiquadResponse, AllPassIsFlatInMagnitude) {
    const double fs = 48000.0;
    const auto a = design(FilterType::AllPass, 1000.0, fs, 1.0, 0.0);
    for (const double f : {50.0, 500.0, 1000.0, 5000.0, 15000.0}) {
        EXPECT_NEAR(magnitude_db(a, f, fs), 0.0, 0.05) << "f=" << f;
    }
}

TEST(BiquadResponse, SymmetricBoostAndCutCancel) {
    // +6 dB then -6 dB at the same f/Q must return to flat: a good check that
    // the peaking coefficients are correctly reciprocal.
    const double fs = 48000.0;
    const auto boost = design(FilterType::Peaking, 1000.0, fs, 1.4142, 6.0);
    const auto cut = design(FilterType::Peaking, 1000.0, fs, 1.4142, -6.0);
    for (const double f : {100.0, 500.0, 1000.0, 2000.0, 10000.0}) {
        EXPECT_NEAR(magnitude_db(boost, f, fs) + magnitude_db(cut, f, fs), 0.0, 0.02)
            << "f=" << f;
    }
}

// ===========================================================================
//  Stability
// ===========================================================================

TEST(BiquadStability, PolesStayInsideUnitCircle) {
    // A stable biquad requires |a2| < 1 and |a1| < 1 + a2.
    for (const double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (const double f : kBands18) {
            for (const double g : {-12.0, -6.0, 6.0, 12.0}) {
                for (const double q : {0.5, 1.4142, 2.871, 8.0}) {
                    const auto c = design(FilterType::Peaking, f, fs, q, g);
                    if (c.is_identity()) continue;
                    EXPECT_LT(std::abs(c.a2), 1.0)
                        << "fs=" << fs << " f=" << f << " g=" << g << " q=" << q;
                    EXPECT_LT(std::abs(c.a1), 1.0 + c.a2 + 1e-9);
                }
            }
        }
    }
}

TEST(BiquadStability, LongRunDoesNotDiverge) {
    // Feed 10 seconds of full-scale noise through a high-Q boost and assert the
    // output stays bounded: catches marginally-stable coefficient bugs.
    const auto c = design(FilterType::Peaking, 60.0, 48000.0, 8.0, 12.0);
    Biquad f{c};
    std::uint32_t seed = 12345;
    float peak = 0.0f;
    for (int i = 0; i < 480000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float in = static_cast<float>(static_cast<double>(seed >> 8) / 8388608.0 - 1.0);
        peak = std::max(peak, std::abs(f.process_one(in)));
    }
    EXPECT_TRUE(std::isfinite(peak));
    EXPECT_LT(peak, 20.0f) << "filter appears to be diverging";
}

TEST(BiquadReset, ClearsDelayLine) {
    Biquad f{design(FilterType::Peaking, 1000.0, 48000.0, 1.0, 12.0)};
    std::vector<float> burst(64, 1.0f);
    f.process_in_place(burst);

    f.reset();
    // After reset, a zero input must produce exactly zero output.
    std::vector<float> zeros(16, 0.0f);
    f.process_in_place(zeros);
    for (const float s : zeros) EXPECT_FLOAT_EQ(s, 0.0f);
}

// ===========================================================================
//  Cascade
// ===========================================================================

TEST(Cascade, DbGainsAddAcrossSections) {
    const double fs = 48000.0;
    BiquadCascade casc;
    casc.resize(2);
    casc.set_coeffs(0, design(FilterType::Peaking, 1000.0, fs, 1.4142, 4.0));
    casc.set_coeffs(1, design(FilterType::Peaking, 1000.0, fs, 1.4142, 3.0));
    EXPECT_NEAR(casc.magnitude_db(1000.0, fs), 7.0, 0.1);
}

TEST(Cascade, SkipsIdentitySections) {
    BiquadCascade casc;
    casc.resize(10);
    // All identity: output must be bit-identical.
    std::vector<float> in(256);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(i) * 0.001f;
    const std::vector<float> original = in;
    casc.process_in_place(in);
    EXPECT_EQ(in, original);
    EXPECT_DOUBLE_EQ(casc.magnitude_db(1000.0, 48000.0), 0.0);
}

TEST(Cascade, ResizeClearsPreviousCoefficients) {
    BiquadCascade casc;
    casc.resize(2);
    casc.set_coeffs(0, design(FilterType::Peaking, 1000.0, 48000.0, 1.0, 6.0));
    casc.resize(3);
    ASSERT_EQ(casc.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_TRUE(casc.has_section(i));
        EXPECT_TRUE(casc.coeffs(i).is_identity());
    }
}

TEST(Cascade, OutOfRangeAccessIsSafe) {
    BiquadCascade casc;
    casc.resize(2);
    EXPECT_FALSE(casc.has_section(99));
    EXPECT_TRUE(casc.coeffs(99).is_identity());     // safe default
    casc.set_coeffs(99, BiquadCoeffs::identity());  // must not crash
}

// ===========================================================================
//  EqSettings validation
// ===========================================================================

TEST(EqSettings, ClampsGainsIntoRange) {
    auto s = graphic_settings(EqMode::Graphic10, {99.0, -99.0, 0.0, 0, 0, 0, 0, 0, 0, 0});
    EXPECT_FALSE(s.clamp_to_valid_ranges());  // it had to change something
    EXPECT_DOUBLE_EQ(s.graphic_gains_db[0], kGainMaxDb);
    EXPECT_DOUBLE_EQ(s.graphic_gains_db[1], kGainMinDb);
}

TEST(EqSettings, ClampsPreamp) {
    auto s = graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0), 50.0);
    s.clamp_to_valid_ranges();
    EXPECT_DOUBLE_EQ(s.preamp_db, kPreampMaxDb);
}

TEST(EqSettings, ResizesGainVectorToMatchMode) {
    auto s = graphic_settings(EqMode::Graphic18, {1.0, 2.0});
    s.clamp_to_valid_ranges();
    EXPECT_EQ(s.graphic_gains_db.size(), kBands18.size());
    EXPECT_DOUBLE_EQ(s.graphic_gains_db[0], 1.0);
    EXPECT_DOUBLE_EQ(s.graphic_gains_db[17], 0.0);
}

TEST(EqSettings, QuantisesToTenthOfADecibel) {
    auto s = graphic_settings(EqMode::Graphic10, std::vector<double>(10, 3.14159));
    s.clamp_to_valid_ranges();
    EXPECT_NEAR(s.graphic_gains_db[0], 3.1, 1e-9);
}

TEST(EqSettings, LimitsParametricBandCount) {
    EqSettings s;
    s.enabled = true;
    s.mode = EqMode::Parametric;
    s.parametric.resize(50);
    s.clamp_to_valid_ranges();
    EXPECT_EQ(s.parametric.size(), kMaxParametricBands);
}

TEST(EqSettings, NeutralDetection) {
    EXPECT_TRUE(EqSettings{}.is_neutral());  // disabled
    EXPECT_TRUE(graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0)).is_neutral());
    EXPECT_FALSE(
        graphic_settings(EqMode::Graphic10, {0, 0, 0, 0, 0.5, 0, 0, 0, 0, 0}).is_neutral());
    EXPECT_FALSE(
        graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0), 3.0).is_neutral());
}

TEST(EqSettings, NonGainBearingParametricFilterIsNeverNeutral) {
    // A low-pass at 0 dB still alters the signal, so it must not be treated as
    // bypassable just because its gain field is zero.
    EqSettings s;
    s.enabled = true;
    s.mode = EqMode::Parametric;
    s.parametric.push_back({FilterType::LowPass, 1000.0, 0.0, 0.707, true});
    EXPECT_FALSE(s.is_neutral());
}

// ===========================================================================
//  Equalizer
// ===========================================================================

TEST(Equalizer, ConfigureRejectsInvalidArguments) {
    Equalizer eq;
    const auto s = graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0));
    EXPECT_FALSE(eq.configure(s, 0, 48000.0).has_value());
    EXPECT_FALSE(eq.configure(s, 2, 0.0).has_value());
    EXPECT_FALSE(eq.configure(s, 2, -1.0).has_value());
    EXPECT_TRUE(eq.configure(s, 2, 48000.0).has_value());
}

TEST(Equalizer, CreatesOneBandPerTableEntry) {
    Equalizer eq;
    ASSERT_TRUE(eq.configure(graphic_settings(EqMode::Graphic10, std::vector<double>(10, 1.0)),
                             2,
                             48000.0)
                    .has_value());
    EXPECT_EQ(eq.band_count(), 10u);
    EXPECT_EQ(eq.channels(), 2u);

    ASSERT_TRUE(eq.configure(graphic_settings(EqMode::Graphic18, std::vector<double>(18, 1.0)),
                             2,
                             48000.0)
                    .has_value());
    EXPECT_EQ(eq.band_count(), 18u);
}

TEST(Equalizer, BypassedWhenDisabledOrNeutral) {
    Equalizer eq;
    EqSettings off;
    ASSERT_TRUE(eq.configure(off, 2, 48000.0).has_value());
    EXPECT_TRUE(eq.is_bypassed());

    ASSERT_TRUE(eq.configure(graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0)),
                             2,
                             48000.0)
                    .has_value());
    EXPECT_TRUE(eq.is_bypassed());

    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {6, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 2, 48000.0)
            .has_value());
    EXPECT_FALSE(eq.is_bypassed());
}

TEST(Equalizer, BypassIsBitExact) {
    // §8.11 test 2: the null test. A bypassed EQ must return the input bits.
    Equalizer eq;
    ASSERT_TRUE(eq.configure(graphic_settings(EqMode::Graphic18, std::vector<double>(18, 0.0)),
                             2,
                             48000.0)
                    .has_value());
    ASSERT_TRUE(eq.is_bypassed());

    std::vector<float> l(1024), r(1024);
    for (std::size_t i = 0; i < l.size(); ++i) {
        l[i] = static_cast<float>(std::sin(static_cast<double>(i) * 0.03) * 0.8);
        r[i] = static_cast<float>(std::cos(static_cast<double>(i) * 0.07) * 0.6);
    }
    const std::vector<float> l0 = l, r0 = r;

    std::array<std::span<float>, 2> planes{std::span<float>{l}, std::span<float>{r}};
    eq.process(planes);

    for (std::size_t i = 0; i < l.size(); ++i) {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(l[i]), std::bit_cast<std::uint32_t>(l0[i]));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(r[i]), std::bit_cast<std::uint32_t>(r0[i]));
    }
}

TEST(Equalizer, PreampAloneIsNotBypassed) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0), -6.0),
                     1,
                     48000.0)
            .has_value());
    EXPECT_FALSE(eq.is_bypassed());
    EXPECT_NEAR(eq.magnitude_db(1000.0), -6.0, 1e-9);
}

TEST(Equalizer, PreampScalesSignal) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0), -6.0206),
                     1,
                     48000.0)
            .has_value());
    std::vector<float> buf(64, 1.0f);
    eq.process_channel(0, buf);
    // -6.0206 dB is exactly a factor of 0.5.
    for (const float s : buf) EXPECT_NEAR(s, 0.5f, 1e-4f);
}

TEST(Equalizer, ResponseCurveIncludesPreampAndBands) {
    Equalizer eq;
    auto s = graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0), -3.0);
    s.graphic_gains_db[5] = 6.0;  // 1 kHz
    ASSERT_TRUE(eq.configure(s, 2, 48000.0).has_value());
    EXPECT_NEAR(eq.magnitude_db(1000.0), 3.0, 0.15);  // 6 - 3
}

TEST(Equalizer, ResponseCurveIsLogSpacedAndCorrectLength) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {6, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 1, 48000.0)
            .has_value());
    const auto curve = eq.response_curve_db(20.0, 20000.0, 200);
    EXPECT_EQ(curve.size(), 200u);
    for (const double v : curve) EXPECT_TRUE(std::isfinite(v));

    EXPECT_TRUE(eq.response_curve_db(20.0, 20000.0, 0).empty());
    EXPECT_TRUE(eq.response_curve_db(0.0, 20000.0, 10).empty());
    EXPECT_TRUE(eq.response_curve_db(20000.0, 20.0, 10).empty());
}

TEST(Equalizer, MeasuredResponseMatchesAnalytic) {
    // End-to-end version of §8.11 test 6, through the whole EQ rather than one
    // section: boost 1 kHz by 9 dB, run an impulse, DFT it, compare.
    const double fs = 48000.0;
    Equalizer eq;
    auto s = graphic_settings(EqMode::Graphic10, std::vector<double>(10, 0.0));
    s.graphic_gains_db[5] = 9.0;  // 1 kHz band
    ASSERT_TRUE(eq.configure(s, 1, fs).has_value());

    constexpr std::size_t kN = 32768;
    std::vector<float> h(kN, 0.0f);
    h[0] = 1.0f;
    eq.process_channel(0, h);

    for (const double f : {100.0, 500.0, 1000.0, 2000.0, 8000.0}) {
        const double w = 2.0 * kPi * f / fs;
        std::complex<double> acc{0.0, 0.0};
        for (std::size_t k = 0; k < kN; ++k) {
            const double angle = -w * static_cast<double>(k);
            acc += static_cast<double>(h[k]) *
                   std::complex<double>{std::cos(angle), std::sin(angle)};
        }
        const double measured = 20.0 * std::log10(std::abs(acc));
        EXPECT_NEAR(eq.magnitude_db(f), measured, 0.25) << "f=" << f;
    }
}

TEST(Equalizer, ChannelsAreIndependent) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {12, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 2, 48000.0)
            .has_value());
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    l[0] = 1.0f;  // impulse on the left only
    std::array<std::span<float>, 2> planes{std::span<float>{l}, std::span<float>{r}};
    eq.process(planes);

    double energy_r = 0.0;
    for (const float s : r) energy_r += static_cast<double>(s) * static_cast<double>(s);
    EXPECT_DOUBLE_EQ(energy_r, 0.0) << "right channel must not be affected by left";
}

TEST(Equalizer, ProcessToleratesFewerPlanesThanChannels) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {6, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 4, 48000.0)
            .has_value());
    std::vector<float> a(32, 0.1f);
    std::array<std::span<float>, 1> planes{std::span<float>{a}};
    eq.process(planes);  // must not read past the provided planes
    EXPECT_TRUE(std::isfinite(a[0]));
}

TEST(Equalizer, ProcessChannelOutOfRangeIsSafe) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {6, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 2, 48000.0)
            .has_value());
    std::vector<float> buf(16, 0.5f);
    eq.process_channel(99, buf);  // must be a no-op, not a crash
    EXPECT_FLOAT_EQ(buf[0], 0.5f);
}

TEST(Equalizer, ParametricModeUsesOnlyEnabledBands) {
    EqSettings s;
    s.enabled = true;
    s.mode = EqMode::Parametric;
    s.parametric.push_back({FilterType::Peaking, 1000.0, 6.0, 1.0, true});
    s.parametric.push_back({FilterType::Peaking, 4000.0, 6.0, 1.0, false});

    Equalizer eq;
    ASSERT_TRUE(eq.configure(s, 1, 48000.0).has_value());
    EXPECT_EQ(eq.band_count(), 1u);
    EXPECT_NEAR(eq.magnitude_db(1000.0), 6.0, 0.1);
    EXPECT_NEAR(eq.magnitude_db(4000.0), 0.0, 1.0);
}

TEST(Equalizer, ResetClearsState) {
    Equalizer eq;
    ASSERT_TRUE(
        eq.configure(
              graphic_settings(EqMode::Graphic10, {12, 0, 0, 0, 0, 0, 0, 0, 0, 0}), 1, 48000.0)
            .has_value());
    std::vector<float> burst(128, 1.0f);
    eq.process_channel(0, burst);
    eq.reset();
    std::vector<float> zeros(32, 0.0f);
    eq.process_channel(0, zeros);
    for (const float s : zeros) EXPECT_NEAR(s, 0.0f, 1e-20f);
}

// ===========================================================================
//  Presets — REQ-AUD-087
// ===========================================================================

TEST(EqPresets, AllRequiredPresetsExist) {
    // REQ-AUD-087 names these as the minimum set.
    for (const char* name : {"Flat",
                             "Rock",
                             "Pop",
                             "Jazz",
                             "Classical",
                             "Dance",
                             "Hip-Hop",
                             "Metal",
                             "Acoustic",
                             "Vocal Boost",
                             "Bass Boost",
                             "Treble Boost",
                             "Loudness",
                             "Small Speakers",
                             "Headphones"}) {
        EXPECT_NE(find_builtin_preset(name), nullptr) << "missing preset: " << name;
    }
}

TEST(EqPresets, LookupIsCaseInsensitive) {
    EXPECT_NE(find_builtin_preset("bass boost"), nullptr);
    EXPECT_NE(find_builtin_preset("BASS BOOST"), nullptr);
    EXPECT_EQ(find_builtin_preset("no such preset"), nullptr);
}

TEST(EqPresets, FlatIsActuallyFlat) {
    const auto* flat = find_builtin_preset("Flat");
    ASSERT_NE(flat, nullptr);
    EXPECT_DOUBLE_EQ(flat->preamp_db, 0.0);
    for (const double g : flat->gains_db) EXPECT_DOUBLE_EQ(g, 0.0);
}

TEST(EqPresets, AllPresetsAreWellFormedAndInRange) {
    for (const auto& p : builtin_presets()) {
        EXPECT_FALSE(p.name.empty());
        EXPECT_EQ(p.gains_db.size(), kBands10.size()) << p.name;
        EXPECT_GE(p.preamp_db, kPreampMinDb) << p.name;
        EXPECT_LE(p.preamp_db, kPreampMaxDb) << p.name;
        for (const double g : p.gains_db) {
            EXPECT_GE(g, kGainMinDb) << p.name;
            EXPECT_LE(g, kGainMaxDb) << p.name;
        }
    }
}

TEST(EqPresets, BoostingPresetsCarryNegativePreampForHeadroom) {
    // A preset that boosts bands without reducing the pre-amp will clip. Every
    // preset with a band above +4 dB must pull the pre-amp down.
    for (const auto& p : builtin_presets()) {
        const double max_gain = *std::max_element(p.gains_db.begin(), p.gains_db.end());
        if (max_gain > 4.0) {
            EXPECT_LT(p.preamp_db, 0.0)
                << p.name << " boosts to " << max_gain << " dB without headroom";
        }
    }
}

TEST(EqPresets, EveryPresetConfiguresAndStaysStable) {
    for (const auto& p : builtin_presets()) {
        EqSettings s;
        s.enabled = true;
        s.mode = p.mode;
        s.preamp_db = p.preamp_db;
        s.graphic_gains_db = p.gains_db;

        Equalizer eq;
        ASSERT_TRUE(eq.configure(s, 2, 44100.0).has_value()) << p.name;
        for (const double f : {20.0, 100.0, 1000.0, 10000.0, 20000.0}) {
            EXPECT_TRUE(std::isfinite(eq.magnitude_db(f))) << p.name << " @ " << f;
        }
    }
}
