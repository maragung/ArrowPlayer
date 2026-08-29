// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#include "audio/dsp/fader.hpp"

#include <algorithm>
#include <cmath>

namespace arrow::audio {

float FaderStage::compute_gain(
    const FadeCurve curve, const FadeDirection direction, const double t) noexcept {
    // Clamp t to [0, 1].
    const double tc = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);

    // For fade-in: we go from 0 to 1.  For fade-out: from 1 to 0.
    // We compute the envelope position and then flip if needed.
    double pos = tc;

    double gain;
    switch (curve) {
        case FadeCurve::Linear:
            // Linear ramp: y = t (then flipped for out).
            gain = pos;
            break;

        case FadeCurve::EqualPower:
            // Equal-power: y = sin(t * pi/2) for in, y = cos(t * pi/2) for out.
            gain = std::sin(pos * (std::acos(-1.0) * 0.5));
            break;

        case FadeCurve::Exponential:
            // Exponential: y = 1 - exp(-t * 5) for in, y = exp(-t * 5) for out.
            // Scale factor 5 gives a reasonably fast reach to 0.99 at t=1.
            gain = 1.0 - std::exp(-pos * 5.0);
            break;

        case FadeCurve::Logarithmic:
            // Logarithmic (human ear model): y = log10(1 + 9*t) / log10(10) = log10(1+9t)
            // This is approximately the inverse of exponential.
            gain = std::log10(1.0 + 9.0 * pos) / std::log10(10.0);
            break;

        case FadeCurve::SCurve: {
            // S-curve: y = 3t² - 2t³ (smoothstep).
            gain = 3.0 * pos * pos - 2.0 * pos * pos * pos;
            break;
        }
    }

    // Flip for fade-out.
    if (direction == FadeDirection::Out) {
        gain = 1.0 - gain;
    }

    return static_cast<float>(gain < 0.0 ? 0.0 : (gain > 1.0 ? 1.0 : gain));
}

void FaderStage::set_fade(FadeSpec spec) noexcept {
    if (spec.duration_frames == 0) {
        cancel();
        return;
    }

    active_.store(true, std::memory_order_release);
    total_.store(spec.duration_frames, std::memory_order_relaxed);
    remaining_.store(spec.duration_frames, std::memory_order_relaxed);
    curve_.store(spec.curve, std::memory_order_relaxed);
    direction_.store(spec.direction, std::memory_order_relaxed);
    gain_.store(compute_gain(spec.curve, spec.direction, 0.0), std::memory_order_relaxed);
}

void FaderStage::cancel() noexcept {
    active_.store(false, std::memory_order_release);
    remaining_.store(0, std::memory_order_relaxed);
    total_.store(0, std::memory_order_relaxed);
    gain_.store(1.0f, std::memory_order_relaxed);
}

void FaderStage::gain_buffer(const std::span<float> out) noexcept {
    if (!active_.load(std::memory_order_acquire)) {
        // All unity.
        std::fill(out.begin(), out.end(), 1.0f);
        return;
    }

    const auto curve = curve_.load(std::memory_order_relaxed);
    const auto direction = direction_.load(std::memory_order_relaxed);
    const auto total = total_.load(std::memory_order_relaxed);
    auto remaining = remaining_.load(std::memory_order_relaxed);

    for (float& g : out) {
        if (remaining == 0) {
            g = 1.0f;
            continue;
        }
        const double t = 1.0 - static_cast<double>(remaining) / static_cast<double>(total);
        g = compute_gain(curve, direction, t);
        if (remaining > 0) --remaining;
    }

    remaining_.store(remaining, std::memory_order_relaxed);
    if (remaining == 0) {
        active_.store(false, std::memory_order_release);
        gain_.store(1.0f, std::memory_order_relaxed);
    }
}

}  // namespace arrow::audio
