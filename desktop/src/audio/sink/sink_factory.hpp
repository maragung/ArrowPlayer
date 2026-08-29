// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

#include <memory>
#include <string_view>

#include "audio/ports/i_audio_sink.hpp"
#include "audio/ports/audio_ports.hpp"

namespace arrow::audio {

enum class SinkPreference {
    Automatic,
    Null,
    Alsa,
    Wasapi,
    Pulse,
};

struct SinkFactoryOptions final {
    SinkPreference preference{SinkPreference::Automatic};
    bool allow_null_fallback{true};
};

[[nodiscard]] Result<std::unique_ptr<IAudioSink>> make_sink(SinkFactoryOptions options = {});

[[nodiscard]] constexpr std::string_view sink_preference_name(
    const SinkPreference preference) noexcept {
    switch (preference) {
        case SinkPreference::Automatic:
            return "automatic";
        case SinkPreference::Null:
            return "null";
        case SinkPreference::Alsa:
            return "alsa";
        case SinkPreference::Wasapi:
            return "wasapi";
        case SinkPreference::Pulse:
            return "pulse";
    }
    return "unknown";
}

}  // namespace arrow::audio
