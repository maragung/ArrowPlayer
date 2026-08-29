// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

// Re-export from the more complete type definitions.
// audio_ports.hpp is the historical location; the authoritative definitions
// now live in idecoder.hpp (StreamInfo, IDecoder) and i_audio_sink.hpp
// (IAudioSink, SinkConfig, DeviceInfo, NegotiatedFormat).
#include "audio/ports/audio_types.hpp"
#include "audio/ports/idecoder.hpp"

namespace arrow::audio {

// Backwards-compatibility aliases so that existing code that includes
// audio_ports.hpp continues to work without modification.
using IDecoder = arrow::audio::IDecoder;
using StreamInfo = arrow::audio::StreamInfo;

// NOTE: IAudioSink and SinkConfig are now in audio_ports/i_audio_sink.hpp.
// Existing code that uses them should be updated to include that header directly.
// The aliases below are intentionally omitted to force callers to update.
// using IAudioSink = arrow::audio::IAudioSink;
// using SinkConfig = arrow::audio::SinkConfig;

}  // namespace arrow::audio
