// SPDX-License-Identifier: MPL-2.0
// Equalizer — spec §8.9.1 (REQ-AUD-080 .. REQ-AUD-088).
//
// Two graphic modes with the exact centre frequencies the spec mandates, plus a
// parametric mode. The same coefficient formulas are shared with the Android
// implementation (REQ-AUD-107/108), which is why design() lives in biquad.hpp
// and is deliberately free of any platform types.

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "audio/dsp/biquad.hpp"
#include "core/error.hpp"

namespace eclipse::audio {

/// REQ-AUD-080 — 10-band graphic EQ, ISO octave centres in Hz.
inline constexpr std::array<double, 10> kBands10 = {
    31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};

/// REQ-AUD-080 — 18-band graphic EQ, half-octave spacing (ratio sqrt(2)), Hz.
inline constexpr std::array<double, 18> kBands18 = {31.5,
                                                    44.5,
                                                    63.0,
                                                    89.0,
                                                    125.0,
                                                    177.0,
                                                    250.0,
                                                    354.0,
                                                    500.0,
                                                    707.0,
                                                    1000.0,
                                                    1414.0,
                                                    2000.0,
                                                    2828.0,
                                                    4000.0,
                                                    5657.0,
                                                    8000.0,
                                                    11314.0};

/// REQ-AUD-081 — gain range and resolution.
inline constexpr double kGainMinDb = -12.0;
inline constexpr double kGainMaxDb = 12.0;
inline constexpr double kGainStepDb = 0.1;

/// REQ-AUD-081 — the separate pre-amp used for headroom management.
inline constexpr double kPreampMinDb = -12.0;
inline constexpr double kPreampMaxDb = 12.0;

/// REQ-AUD-086 — parametric mode limits.
inline constexpr std::size_t kMaxParametricBands = 10;
inline constexpr double kParametricGainMinDb = -24.0;
inline constexpr double kParametricGainMaxDb = 24.0;
inline constexpr double kParametricQMin = 0.1;
inline constexpr double kParametricQMax = 18.0;
inline constexpr double kParametricFreqMinHz = 20.0;
inline constexpr double kParametricFreqMaxHz = 20000.0;

/// REQ-AUD-085 — coefficient cross-ramp duration when gains change, in
/// milliseconds. Swapping coefficients instantaneously produces zipper noise.
inline constexpr double kCoeffRampMs = 32.0;

enum class EqMode {
    Graphic10,   ///< kBands10, Q from one-octave bandwidth
    Graphic18,   ///< kBands18, Q from half-octave bandwidth
    Parametric,  ///< user-defined bands
};

/// One parametric band (REQ-AUD-086).
struct ParametricBand {
    FilterType type = FilterType::Peaking;
    double freq_hz = 1000.0;
    double gain_db = 0.0;
    double q = 1.0;
    bool enabled = true;
};

/// The complete, serialisable EQ state. This is what a preset stores and what
/// the parameter-snapshot publication in REQ-AUD-016 hands to the RT thread.
struct EqSettings {
    bool enabled = false;
    EqMode mode = EqMode::Graphic10;
    double preamp_db = 0.0;

    /// Graphic gains, indexed to match kBands10 / kBands18. Extra entries are
    /// ignored; missing entries are treated as 0 dB.
    std::vector<double> graphic_gains_db;

    std::vector<ParametricBand> parametric;

    /// Clamps every value into its documented range. Returns false if anything
    /// had to be changed, so a caller loading an untrusted preset can report it.
    bool clamp_to_valid_ranges();

    /// True when this configuration is audibly a no-op, so the whole stage can
    /// be skipped (REQ-AUD-005).
    [[nodiscard]] bool is_neutral() const noexcept;
};

/// Centre frequencies for a graphic mode. Empty for Parametric.
[[nodiscard]] std::span<const double> bands_for_mode(EqMode mode) noexcept;

/// Bandwidth in octaves for a graphic mode, feeding q_for_bandwidth_octaves().
[[nodiscard]] double bandwidth_octaves_for_mode(EqMode mode) noexcept;

/// Named presets — REQ-AUD-087.
struct EqPreset {
    std::string name;
    EqMode mode = EqMode::Graphic10;
    double preamp_db = 0.0;
    std::vector<double> gains_db;
};

/// The built-in preset list required by REQ-AUD-087.
[[nodiscard]] const std::vector<EqPreset>& builtin_presets();

/// Looks up a built-in preset by name, case-insensitively.
[[nodiscard]] const EqPreset* find_builtin_preset(std::string_view name);

/// A multi-channel equalizer.
///
/// Ownership/threading: configure() is NOT RT-safe (it allocates and computes
/// transcendental functions). process() IS RT-safe. The intended pattern is that
/// the UI thread calls configure() and publishes the result per REQ-AUD-016.
class Equalizer {
  public:
    /// Allocates per-channel cascades. NOT RT-safe.
    /// `channels` and `sample_rate_hz` must be > 0.
    Status configure(const EqSettings& settings, std::size_t channels, double sample_rate_hz);

    /// Clears all delay lines. Call on seek and track change.
    void reset() noexcept;

    /// RT-SAFE: processes planar float32 in place. `planes.size()` must equal
    /// the configured channel count; extra planes are ignored.
    ///
    /// Applies the pre-amp first, then the band cascade, matching the order the
    /// pre-amp exists for: creating headroom before boosting bands.
    void process(std::span<std::span<float>> planes) noexcept;

    /// RT-SAFE: single-channel convenience overload.
    void process_channel(std::size_t channel, std::span<float> samples) noexcept;

    /// True when configure() produced an audible no-op.
    [[nodiscard]] bool is_bypassed() const noexcept { return bypassed_; }

    [[nodiscard]] std::size_t channels() const noexcept { return cascades_.size(); }

    [[nodiscard]] std::size_t band_count() const noexcept { return band_count_; }

    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }

    /// Combined response of pre-amp + all bands, in dB, at `freq_hz`.
    /// REQ-AUD-088: this is the real cascaded transfer function, which is what
    /// the UI must plot and what §8.11 test 6 verifies against measurement.
    [[nodiscard]] double magnitude_db(double freq_hz) const noexcept;

    /// Samples the response across a log-spaced grid, for plotting.
    [[nodiscard]] std::vector<double> response_curve_db(double from_hz,
                                                        double to_hz,
                                                        std::size_t points) const;

    /// True when `index` names a designed band.
    [[nodiscard]] bool has_band(std::size_t index) const noexcept {
        return index < designed_.size();
    }

    /// Coefficients of band `index`, or the identity when out of range.
    [[nodiscard]] BiquadCoeffs band_coeffs(std::size_t index) const noexcept;

  private:
    std::vector<BiquadCascade> cascades_;
    std::vector<BiquadCoeffs> designed_;  ///< one per band, shared by channels
    double preamp_linear_{1.0};
    double preamp_db_{0.0};
    double sample_rate_{48000.0};
    std::size_t band_count_{0};
    bool bypassed_{true};
};

}  // namespace eclipse::audio
