// SPDX-License-Identifier: MPL-2.0
#include "audio/dsp/equalizer.hpp"

#include <algorithm>
#include <cmath>

#include "core/text.hpp"

namespace eclipse::audio {
namespace {

constexpr double clampd(double v, double lo, double hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Quantises to the documented 0.1 dB step (REQ-AUD-081) so that a preset saved
/// and reloaded is bit-identical, and so the UI slider and the coefficients
/// never disagree.
double quantise_gain(double db) noexcept {
    return std::round(db / kGainStepDb) * kGainStepDb;
}

}  // namespace

std::span<const double> bands_for_mode(EqMode mode) noexcept {
    switch (mode) {
        case EqMode::Graphic10:  return {kBands10.data(), kBands10.size()};
        case EqMode::Graphic18:  return {kBands18.data(), kBands18.size()};
        case EqMode::Parametric: return {};
    }
    return {};
}

double bandwidth_octaves_for_mode(EqMode mode) noexcept {
    switch (mode) {
        case EqMode::Graphic10:  return 1.0;   // Q ~= 1.4142
        case EqMode::Graphic18:  return 0.5;   // Q ~= 2.8710
        case EqMode::Parametric: return 1.0;
    }
    return 1.0;
}

// ---------------------------------------------------------------------------
//  EqSettings
// ---------------------------------------------------------------------------

bool EqSettings::clamp_to_valid_ranges() {
    bool changed = false;

    const double p = clampd(preamp_db, kPreampMinDb, kPreampMaxDb);
    if (p != preamp_db) { preamp_db = p; changed = true; }

    const auto bands = bands_for_mode(mode);
    if (mode != EqMode::Parametric) {
        if (graphic_gains_db.size() != bands.size()) {
            graphic_gains_db.resize(bands.size(), 0.0);
            changed = true;
        }
        for (double& g : graphic_gains_db) {
            const double c = quantise_gain(clampd(g, kGainMinDb, kGainMaxDb));
            if (c != g) { g = c; changed = true; }
        }
    }

    if (parametric.size() > kMaxParametricBands) {
        parametric.resize(kMaxParametricBands);
        changed = true;
    }
    for (auto& b : parametric) {
        const double f = clampd(b.freq_hz, kParametricFreqMinHz, kParametricFreqMaxHz);
        const double g = clampd(b.gain_db, kParametricGainMinDb, kParametricGainMaxDb);
        const double q = clampd(b.q, kParametricQMin, kParametricQMax);
        if (f != b.freq_hz) { b.freq_hz = f; changed = true; }
        if (g != b.gain_db) { b.gain_db = g; changed = true; }
        if (q != b.q)       { b.q = q;       changed = true; }
    }
    return !changed;
}

bool EqSettings::is_neutral() const noexcept {
    if (!enabled) return true;
    if (std::abs(preamp_db) > 1e-9) return false;

    if (mode == EqMode::Parametric) {
        for (const auto& b : parametric) {
            if (!b.enabled) continue;
            const bool gain_bearing = (b.type == FilterType::Peaking ||
                                       b.type == FilterType::LowShelf ||
                                       b.type == FilterType::HighShelf);
            // A non-gain-bearing filter (low-pass, notch, ...) always alters the
            // signal, so it is never neutral regardless of gain.
            if (!gain_bearing) return false;
            if (std::abs(b.gain_db) > 1e-9) return false;
        }
        return true;
    }

    for (const double g : graphic_gains_db) {
        if (std::abs(g) > 1e-9) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Presets — REQ-AUD-087
// ---------------------------------------------------------------------------

const std::vector<EqPreset>& builtin_presets() {
    // 10-band gains aligned to kBands10:
    //   31.5  63  125  250  500  1k   2k   4k   8k   16k
    static const std::vector<EqPreset> kPresets = {
        {"Flat",          EqMode::Graphic10, 0.0, { 0.0,  0.0,  0.0,  0.0, 0.0,  0.0,  0.0,  0.0,  0.0,  0.0}},
        {"Rock",          EqMode::Graphic10, -1.0,{ 5.0,  4.0,  3.0,  1.5, -0.5, -1.0,  0.5,  3.0,  4.5,  5.0}},
        {"Pop",           EqMode::Graphic10, -1.0,{-1.5, -1.0,  0.0,  2.0,  4.0,  4.0,  2.0,  0.0, -1.0, -1.5}},
        {"Jazz",          EqMode::Graphic10, 0.0, { 3.0,  2.0,  1.0,  2.0, -1.0, -1.0,  0.0,  1.0,  2.5,  3.5}},
        {"Classical",     EqMode::Graphic10, 0.0, { 4.0,  3.0,  2.0,  0.0, -1.0, -1.0,  0.0,  2.0,  3.0,  4.0}},
        {"Dance",         EqMode::Graphic10, -2.0,{ 6.0,  5.5,  3.5,  0.0, -1.0, -2.0,  1.0,  3.5,  5.0,  4.5}},
        {"Hip-Hop",       EqMode::Graphic10, -2.0,{ 6.5,  5.0,  2.0,  1.0, -1.0, -1.0,  1.0,  2.0,  3.0,  3.5}},
        {"Metal",         EqMode::Graphic10, -2.0,{ 5.0,  4.0,  1.0, -1.0, -2.0,  0.0,  2.5,  4.5,  5.0,  4.0}},
        {"Acoustic",      EqMode::Graphic10, 0.0, { 3.5,  3.0,  2.0,  1.0,  0.5,  0.5,  1.5,  2.5,  3.0,  2.5}},
        {"Vocal Boost",   EqMode::Graphic10, -1.0,{-2.0, -1.5, -0.5,  1.5,  4.0,  4.5,  3.5,  1.5, -0.5, -1.5}},
        {"Bass Boost",    EqMode::Graphic10, -3.0,{ 8.0,  6.5,  4.5,  2.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0}},
        {"Treble Boost",  EqMode::Graphic10, -3.0,{ 0.0,  0.0,  0.0,  0.0,  0.0,  1.0,  3.0,  5.5,  7.0,  8.0}},
        {"Loudness",      EqMode::Graphic10, -3.0,{ 7.0,  5.5,  2.5,  0.0, -1.5, -1.5,  0.0,  2.5,  5.5,  7.0}},
        {"Small Speakers",EqMode::Graphic10, -1.0,{-4.0, -2.0,  1.0,  3.0,  4.0,  3.0,  1.5,  0.5, -1.0, -3.0}},
        {"Headphones",    EqMode::Graphic10, -1.0,{ 4.0,  3.0,  1.0, -0.5, -1.5, -1.0,  0.5,  2.0,  3.5,  4.0}},
    };
    return kPresets;
}

const EqPreset* find_builtin_preset(std::string_view name) {
    for (const auto& p : builtin_presets()) {
        if (text::iequals(p.name, name)) return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
//  Equalizer
// ---------------------------------------------------------------------------

Status Equalizer::configure(const EqSettings& settings,
                            std::size_t channels,
                            double sample_rate_hz) {
    if (channels == 0) {
        return err(ErrorCode::InvalidArgument,
                   "The equalizer needs at least one channel.",
                   "channels=0");
    }
    if (!(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
        return err(ErrorCode::InvalidArgument,
                   "The equalizer needs a valid sample rate.",
                   "sample_rate=" + std::to_string(sample_rate_hz));
    }

    sample_rate_ = sample_rate_hz;
    preamp_db_   = clampd(settings.preamp_db, kPreampMinDb, kPreampMaxDb);
    preamp_linear_ = std::pow(10.0, preamp_db_ / 20.0);

    designed_.clear();

    if (settings.mode == EqMode::Parametric) {
        const double bw = bandwidth_octaves_for_mode(settings.mode);
        (void)bw;
        for (const auto& b : settings.parametric) {
            if (!b.enabled) continue;
            designed_.push_back(design(b.type,
                                       clampd(b.freq_hz, kParametricFreqMinHz, kParametricFreqMaxHz),
                                       sample_rate_,
                                       clampd(b.q, kParametricQMin, kParametricQMax),
                                       clampd(b.gain_db, kParametricGainMinDb, kParametricGainMaxDb)));
        }
    } else {
        const auto bands = bands_for_mode(settings.mode);
        // REQ-AUD-083: Q comes from band spacing, never hard-coded.
        const double q = q_for_bandwidth_octaves(bandwidth_octaves_for_mode(settings.mode));
        for (std::size_t i = 0; i < bands.size(); ++i) {
            const double gain = (i < settings.graphic_gains_db.size())
                                    ? quantise_gain(clampd(settings.graphic_gains_db[i],
                                                           kGainMinDb, kGainMaxDb))
                                    : 0.0;
            // design() returns identity for 0 dB and for bands above
            // 0.95*Nyquist (REQ-AUD-084), so both cases skip at process time.
            designed_.push_back(design(FilterType::Peaking, bands[i], sample_rate_, q, gain));
        }
    }

    band_count_ = designed_.size();

    cascades_.assign(channels, BiquadCascade{});
    for (auto& casc : cascades_) {
        casc.resize(band_count_);
        for (std::size_t i = 0; i < band_count_; ++i) casc.set_coeffs(i, designed_[i]);
    }

    // Determine true bypass: disabled, or neutral gains and a unity pre-amp.
    bool any_active = false;
    for (const auto& c : designed_) {
        if (!c.is_identity()) { any_active = true; break; }
    }
    const bool unity_preamp = std::abs(preamp_db_) < 1e-9;
    bypassed_ = !settings.enabled || (!any_active && unity_preamp);

    return ok();
}

void Equalizer::reset() noexcept {
    for (auto& c : cascades_) c.reset();
}

void Equalizer::process_channel(std::size_t channel, std::span<float> samples) noexcept {
    if (bypassed_ || channel >= cascades_.size()) return;

    if (preamp_linear_ != 1.0) {
        const float g = static_cast<float>(preamp_linear_);
        for (float& s : samples) s *= g;
    }
    cascades_[channel].process_in_place(samples);
}

void Equalizer::process(std::span<std::span<float>> planes) noexcept {
    // REQ-AUD-005: a bypassed stage is skipped entirely, not run at unity.
    if (bypassed_) return;

    const std::size_t n = planes.size() < cascades_.size() ? planes.size() : cascades_.size();
    for (std::size_t ch = 0; ch < n; ++ch) {
        process_channel(ch, planes[ch]);
    }
}

double Equalizer::magnitude_db(double freq_hz) const noexcept {
    if (bypassed_) return 0.0;
    // Pre-amp is a frequency-independent gain, so it adds a constant in dB.
    double total = preamp_db_;
    for (const auto& c : designed_) {
        if (c.is_identity()) continue;
        total += audio::magnitude_db(c, freq_hz, sample_rate_);
    }
    return total;
}

std::vector<double> Equalizer::response_curve_db(double from_hz,
                                                 double to_hz,
                                                 std::size_t points) const {
    std::vector<double> out;
    if (points == 0 || !(from_hz > 0.0) || !(to_hz > from_hz)) return out;

    out.reserve(points);
    const double log_from = std::log10(from_hz);
    const double log_to   = std::log10(to_hz);
    const double step = (points > 1) ? (log_to - log_from) / static_cast<double>(points - 1) : 0.0;

    for (std::size_t i = 0; i < points; ++i) {
        const double f = std::pow(10.0, log_from + step * static_cast<double>(i));
        out.push_back(magnitude_db(f));
    }
    return out;
}

BiquadCoeffs Equalizer::band_coeffs(std::size_t index) const noexcept {
    return index < designed_.size() ? designed_[index] : BiquadCoeffs::identity();
}

}  // namespace eclipse::audio
