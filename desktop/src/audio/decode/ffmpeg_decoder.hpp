// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/ports/idecoder.hpp"

#if defined(ARROW_HAVE_FFMPEG)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#endif

namespace arrow::audio {

/// FFmpeg-backed audio decoder.
///
/// Implements IDecoder for all formats supported by the FFmpeg build:
/// MP3, FLAC, AAC, Opus, Vorbis, WavPack, APE, WAV, AIFF, and more.
///
/// Content-based format detection uses av_probe_input_buffer3 so the file
/// extension is advisory rather than definitive (REQ-AUD-027).
///
/// Gapless info is extracted per §8.4:
///   - LAME tag for MP3 (REQ-AUD-037)
///   - iTunSMPB for AAC/M4A (REQ-AUD-040)
///   - OpusHead pre-skip for Opus (REQ-AUD-043)
///   - native frame count for lossless formats (REQ-AUD-044)
///
/// ReplayGain 1.0 / 2.0 tags are read from metadata dictionaries.
class FfmpegDecoder final : public IDecoder {
  public:
    FfmpegDecoder() = default;

    // IDecoder contract
    [[nodiscard]] Result<StreamInfo> open(const std::filesystem::path& path) override;
    [[nodiscard]] Result<std::size_t> read(PlanarFrames destination) override;
    [[nodiscard]] Status seek(std::uint64_t frame) override;
    void close() noexcept override;
    [[nodiscard]] std::optional<StreamInfo> stream_info() const noexcept override;
    [[nodiscard]] bool is_open() const noexcept override;

  private:
    // FFmpeg objects — RAII helpers defined in the .cpp
#if defined(ARROW_HAVE_FFMPEG)
    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* dec_ctx_ = nullptr;
    const AVCodec* codec_ = nullptr;
    int stream_index_ = -1;
    AVPacket* pkt_ = nullptr;
    AVFrame* frame_ = nullptr;

    /// Converts FFmpeg error codes to arrow::Error with proper classification
    /// (REQ-AUD-027).
    Error ffmpeg_error(int errcode, std::string_view operation) const;

    /// Reads ReplayGain tags from the format context's metadata dictionary.
    ReplayGainTags read_replaygain_tags() const;

    /// Reads gapless / trim info from the format context.
    std::optional<GaplessInfo> read_gapless_info() const;

    /// Detects the codec type from the opened stream and populates codec fields.
    void detect_codec_type();
#else
    // Stub when FFmpeg is not available — the class still has a valid vtable.
    void* ctx_ = nullptr;
#endif

    StreamInfo info_{};
    bool opened_ = false;

    /// Decoded samples that could not be returned because the caller's buffer
    /// was too small.  These are returned on the next read() call.
    std::vector<float> carryover_{};
    std::size_t carryover_offset_ = 0;

    /// Resampling buffer when the decoder output doesn't match our target.
    std::vector<float> resample_buf_{};
};

}  // namespace arrow::audio
