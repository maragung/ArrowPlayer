// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <memory>
#include <string_view>

#include "audio/ports/audio_ports.hpp"

namespace eclipse::audio {

enum class SinkPreference {
    Automatic,
    Null,
    Alsa,
};

struct SinkFactoryOptions final {
    SinkPreference preference{SinkPreference::Automatic};
    bool allow_null_fallback{true};
};

[[nodiscard]] Result<std::unique_ptr<IAudioSink>> make_sink(
    SinkFactoryOptions options = {});

[[nodiscard]] constexpr std::string_view sink_preference_name(
    const SinkPreference preference) noexcept {
    switch (preference) {
        case SinkPreference::Automatic: return "automatic";
        case SinkPreference::Null: return "null";
        case SinkPreference::Alsa: return "alsa";
    }
    return "unknown";
}

}  // namespace eclipse::audio
