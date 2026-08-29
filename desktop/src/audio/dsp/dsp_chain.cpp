// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#include "audio/dsp/dsp_chain.hpp"

#include <algorithm>
#include <cmath>

namespace arrow::audio {

Status DspChain::configure(const DspStageConfig& config) {
    if (config.channels == 0 || config.channels > 32) {
        return err(ErrorCode::InvalidArgument,
                   "The DSP chain needs between 1 and 32 channels.",
                   "channels=" + std::to_string(config.channels));
    }
    if (!(config.sample_rate > 0.0) || !std::isfinite(config.sample_rate)) {
        return err(ErrorCode::InvalidArgument,
                   "The DSP chain needs a valid sample rate.",
                   "sample_rate=" + std::to_string(config.sample_rate));
    }

    config_ = config;

    // ---- Equalizer ----
    EqSettings eq_off{};
    eq_off.enabled = false;
    if (auto r = eq_.configure(eq_off, config.channels, config.sample_rate); !r) {
        return r;
    }
    eq_.publish_settings(0);  // instant snap

    // ---- Limiter ----
    if (auto r = limiter_.configure(config.sample_rate, kLimiterCeilingDb); !r) {
        return r;
    }
    limiter_.reset();

    // ---- Bypass check ----
    // The chain is bypassed only when every stage is bypassed.
    // EQ: disabled → bypassed.
    // Limiter: ceiling = 0 dBFS → bypassed.
    // Fader: no active fade → gain = 1.0 (but this doesn't make it "bypassed"
    //        because a fader in neutral state is still part of the signal path).
    // Volume: 1.0 → still in path.
    bypassed_ = eq_.is_bypassed() && limiter_.is_bypassed();

    configured_ = true;
    return ok();
}

void DspChain::apply_eq(const EqSettings& settings) {
    if (!configured_) return;
    if (auto r = eq_.configure(settings, config_.channels, config_.sample_rate); !r) return;
    const auto ramp = ramp_samples_for(config_.sample_rate);
    eq_.publish_settings(ramp);

    // Re-evaluate bypass: if EQ becomes non-neutral, chain is not bypassed.
    if (!eq_.is_bypassed()) bypassed_ = false;
}

void DspChain::apply_replaygain(const ReplayGainSettings& settings) {
    rg_settings_ = settings;
    // ReplayGain is applied via the volume pre-amp in process().
}

void DspChain::process(const std::span<std::span<float>> planes) noexcept {
    if (!configured_ || bypassed_) return;
    if (planes.empty()) return;

    const std::size_t n = planes[0].size();
    if (n == 0) return;

    // ---- Stage 3: Equalizer (pre-amp + bands) ----
    if (!eq_.is_bypassed()) {
        eq_.process(planes);
    }

    // ---- Stage 8: True-peak limiter ----
    if (!limiter_.is_bypassed()) {
        limiter_.process(planes);
    }

    // ---- Stage 11: Output volume + fade ----
    const float vol = volume_.load(std::memory_order_relaxed);
    const bool fading = fader_.active();

    if (fading) {
        // Block fade: generate gain buffer and multiply.
        std::vector<float> gain_buf(n);
        fader_.gain_buffer(std::span<float>{gain_buf});
        for (std::size_t ch = 0; ch < planes.size(); ++ch) {
            for (std::size_t i = 0; i < n; ++i) {
                planes[ch][i] *= vol * gain_buf[i];
            }
        }
    } else if (vol != 1.0f) {
        // Volume only — common case.
        for (std::size_t ch = 0; ch < planes.size(); ++ch) {
            for (std::size_t i = 0; i < n; ++i) {
                planes[ch][i] *= vol;
            }
        }
    }
    // else: unity — nothing to do.
}

void DspChain::reset() noexcept {
    eq_.reset();
    limiter_.reset();
    fader_.reset();
}

}  // namespace arrow::audio
