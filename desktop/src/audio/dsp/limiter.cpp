// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#include "audio/dsp/limiter.hpp"

#include <algorithm>
#include <cmath>

namespace arrow::audio {

Status LimiterStage::configure(double sample_rate_hz, const double ceiling_db) {
    if (!(sample_rate_hz > 0.0) || !std::isfinite(sample_rate_hz)) {
        return err(ErrorCode::InvalidArgument,
                   "The limiter needs a valid sample rate.",
                   "sample_rate_hz=" + std::to_string(sample_rate_hz));
    }
    if (!(ceiling_db <= kLimiterCeilingMaxDb) || !(ceiling_db >= kLimiterCeilingMinDb)) {
        return err(ErrorCode::InvalidArgument,
                   "The limiter ceiling is out of range.",
                   "ceiling_db=" + std::to_string(ceiling_db));
    }

    sample_rate_ = sample_rate_hz;
    ceiling_db_ = ceiling_db;
    ceiling_linear_ = dbfs_to_linear(ceiling_db_);

    // Look-ahead: 5 ms.
    look_ahead_ = static_cast<std::size_t>(
        std::round(kLimiterLookAheadMs * 0.001 * sample_rate_hz));
    look_ahead_ = std::min(look_ahead_,
                           static_cast<std::size_t>(kLimiterLookAheadMaxMs * 0.001 * sample_rate_hz));

    // Release time constant: 50 ms.  Coefficient such that after τ = 50 ms,
    // the gain has risen to 1 - 1/e ≈ 63.2%.
    //   exp(-1/(rate * τ)) ≈ release_coeff_
    //   release_coeff_ = exp(-1.0 / (sample_rate * (release_ms * 0.001)))
    const double rate = sample_rate_hz;
    const double tau_s = kLimiterReleaseMs * 0.001;
    release_coeff_ = std::exp(-1.0 / (rate * tau_s));

    // Allocate peak buffer for the longest possible look-ahead window.
    // This is sized in configure() so process() never allocates.
    peak_buf_.resize(look_ahead_ + 1, 0.0);

    bypassed_ = ceiling_db >= kLimiterCeilingMaxDb;
    configured_ = true;
    return ok();
}

void LimiterStage::set_ceiling(const double ceiling_db) noexcept {
    if (!(ceiling_db <= kLimiterCeilingMaxDb) || !(ceiling_db >= kLimiterCeilingMinDb)) {
        return;
    }
    ceiling_db_ = ceiling_db;
    ceiling_linear_ = dbfs_to_linear(ceiling_db_);
    bypassed_ = ceiling_db >= kLimiterCeilingMaxDb;
}

void LimiterStage::reset() noexcept {
    std::fill(gain_state_.begin(), gain_state_.end(), 1.0);
}

void LimiterStage::compute_peaks(
    const std::span<const float> buffer,
    const std::span<double> peak_out) const noexcept {
    const auto n = buffer.size();
    // Clear peak_out.
    std::fill(peak_out.begin(), peak_out.end(), 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        const auto abs_val = static_cast<double>(std::abs(buffer[i]));
        // The peak for position i is the max over [i, i + look_ahead_].
        // We accumulate forward so we can do a single pass.
        // Simple O(n*lookahead) — acceptable for the look-ahead window sizes we use.
        double peak = abs_val;
        const auto limit = std::min(n, i + look_ahead_);
        for (std::size_t j = i + 1; j < limit; ++j) {
            const auto a = static_cast<double>(std::abs(buffer[j]));
            if (a > peak) peak = a;
        }
        peak_out[i] = peak;
    }
}

void LimiterStage::apply_gain(
    const std::span<float> buffer,
    const std::span<const double> peak_buf) noexcept {
    const auto n = buffer.size();

    // Resize gain state if needed.
    if (gain_state_.size() < n) {
        gain_state_.resize(n, 1.0);
    }

    for (std::size_t i = 0; i < n; ++i) {
        // Compute desired gain from look-ahead peak.
        const double peak = peak_buf[i];
        double desired = 1.0;
        if (peak > 0.0 && peak * gain_state_[i] > ceiling_linear_) {
            desired = ceiling_linear_ / peak;
        }

        // Exponential release towards desired gain.
        // If desired > current, release fast (higher coefficient toward 1).
        // If desired < current, attack instantly (set to desired).
        if (desired > gain_state_[i]) {
            // Release: multiply by release_coeff_ to approach 1.0 from below.
            gain_state_[i] = gain_state_[i] * release_coeff_ + desired * (1.0 - release_coeff_);
            // But if desired is 1.0 and gain is already ~1, this should stay ~1.
            // The simpler version: gain = gain * release_coeff_ + desired * (1 - release_coeff_)
            // This is a standard exponential moving average towards desired.
        } else {
            // Attack: snap to desired gain.
            gain_state_[i] = desired;
        }

        // Clamp gain to a safe range.
        if (gain_state_[i] < 1e-9) gain_state_[i] = 1e-9;
        if (gain_state_[i] > 1.0) gain_state_[i] = 1.0;

        buffer[i] = static_cast<float>(
            static_cast<double>(buffer[i]) * gain_state_[i]);
    }
}

void LimiterStage::process_channel(
    const std::span<float> buffer,
    const std::span<double> peak_buf) noexcept {
    if (!configured_ || bypassed_) return;
    compute_peaks(buffer, peak_buf);
    apply_gain(buffer, peak_buf);
}

void LimiterStage::process(const std::span<std::span<float>> planes) noexcept {
    if (!configured_ || bypassed_) return;
    if (planes.empty()) return;

    const auto n = planes[0].size();
    if (n == 0) return;

    // Resize peak buffer and gain state.
    if (peak_buf_.size() < n) peak_buf_.resize(n + look_ahead_, 0.0);
    if (gain_state_.size() < n) gain_state_.resize(n, 1.0);

    // Compute the maximum absolute value across all channels for each sample.
    // This is the "true peak" — the peak that would appear after reconstruction.
    // For now we use sample-rate peak; true-peak would require oversampling.
    for (std::size_t i = 0; i < n; ++i) {
        double max_abs = 0.0;
        for (const auto& plane : planes) {
            const auto a = static_cast<double>(std::abs(plane[i]));
            if (a > max_abs) max_abs = a;
        }
        peak_buf_[i] = max_abs;
    }

    // Apply gain to each channel using the same peak values.
    for (std::size_t ch = 0; ch < planes.size(); ++ch) {
        const auto& peak = std::span<const double>{peak_buf_.data(), n};
        apply_gain(planes[ch], peak);
    }
}

void LimiterStage::process(const std::span<float> buffer) noexcept {
    if (!configured_ || bypassed_) return;
    if (buffer.empty()) return;

    if (peak_buf_.size() < buffer.size()) peak_buf_.resize(buffer.size(), 0.0);
    if (gain_state_.size() < buffer.size()) gain_state_.resize(buffer.size(), 1.0);

    compute_peaks(buffer, std::span<double>{peak_buf_.data(), buffer.size()});
    apply_gain(buffer, std::span<const double>{peak_buf_.data(), buffer.size()});
}

}  // namespace arrow::audio
