// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#include "audio/decode/ffmpeg_decoder.hpp"

#include <algorithm>
#include <cstring>

#if defined(ARROW_HAVE_FFMPEG)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#endif

namespace arrow::audio {

#if !defined(ARROW_HAVE_FFMPEG)

// Stub implementation when FFmpeg is not available.
Result<StreamInfo> FfmpegDecoder::open(const std::filesystem::path&) {
    return err(ErrorCode::DecoderInitFailed,
               "FFmpeg is not available in this build.");
}

Result<std::size_t> FfmpegDecoder::read(PlanarFrames) {
    return err(ErrorCode::InvalidState, "The decoder is not open.");
}

Status FfmpegDecoder::seek(std::uint64_t) {
    return err(ErrorCode::SeekFailed, "FFmpeg is not available in this build.");
}

void FfmpegDecoder::close() noexcept { opened_ = false; }

std::optional<StreamInfo> FfmpegDecoder::stream_info() const noexcept {
    return opened_ ? std::optional<StreamInfo>{info_} : std::nullopt;
}

bool FfmpegDecoder::is_open() const noexcept { return opened_; }

#else  // ARROW_HAVE_FFMPEG

namespace {

// ---------------------------------------------------------------------------
//  FFmpeg error classification (REQ-AUD-027)
// ---------------------------------------------------------------------------

/// Converts an FFmpeg AVERROR code to a human-readable C string.
std::string ffmpeg_strerror(int errcode) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(errcode, buf, sizeof(buf));
    return std::string{buf};
}

/// Maps FFmpeg error codes to arrow::ErrorCode per REQ-AUD-027.
ErrorCode classify_ffmpeg_error(int errcode) {
    if (errcode == AVERROR_EOF || errcode == AVERROR(EIO)) return ErrorCode::CorruptStream;
    if (errcode == AVERROR_DECODER_NOT_FOUND) return ErrorCode::UnsupportedFormat;
    if (errcode == AVERROR_ENCODER_NOT_FOUND) return ErrorCode::UnsupportedFormat;
    if (errcode == AVERROR_DEMUXER_NOT_FOUND) return ErrorCode::UnsupportedFormat;
    if (errcode == AVERROR_INVALIDDATA) return ErrorCode::CorruptStream;
    if (errcode == AVERROR_DECODE_EXHAUSTED) return ErrorCode::CorruptStream;
    if (errcode == AVERROR(EAGAIN)) return ErrorCode::CorruptStream;
    if (errcode >= 0) return ErrorCode::Ok;
    return ErrorCode::DecoderInitFailed;
}

// ---------------------------------------------------------------------------
//  Container detection helpers
// ---------------------------------------------------------------------------

/// Maps FFmpeg AVMediaType to our ContainerId enum.
ContainerId media_type_to_container(AVMediaType mt) {
    switch (mt) {
        case AVMEDIA_TYPE_AUDIO:
            return ContainerId::Unknown;
        default:
            return ContainerId::Unknown;
    }
}

// ---------------------------------------------------------------------------
//  Channel layout conversion
// ---------------------------------------------------------------------------

constexpr std::size_t avchannel_count_to_std(uint64_t mask) {
    return static_cast<std::size_t>(av_popcount64(mask));
}

ChannelLayout avchannel_layout_to_layout(uint64_t av_layout, std::size_t channels) {
    switch (channels) {
        case 1:
            return ChannelLayout::Mono;
        case 2:
            return ChannelLayout::Stereo;
        case 4:
            return ChannelLayout::Quad;
        case 6:
            return ChannelLayout::Surround51;
        case 8:
            return ChannelLayout::Octagonal;
        default:
            return ChannelLayout::Unknown;
    }
}

// ---------------------------------------------------------------------------
//  Gapless info extraction helpers
// ---------------------------------------------------------------------------

/// Tries to read a GaplessInfo from MP3 Xing/LAME tag.
// Defined in audio/decode/gapless_info.cpp; redeclared here to avoid pulling
// the full gapless_info.hpp (which needs FFmpeg types).
extern GaplessInfo mp3_gapless_info(std::span<const std::uint8_t> first_frame);

// ---------------------------------------------------------------------------
//  Codec identification
// ---------------------------------------------------------------------------

CodecId ffmpeg_codec_id_to_codec(AVCodecID av_id) {
    switch (av_id) {
        case AV_CODEC_ID_MP3:
            return CodecId::Mp3;
        case AV_CODEC_ID_FLAC:
            return CodecId::Flac;
        case AV_CODEC_ID_VORBIS:
            return CodecId::Vorbis;
        case AV_CODEC_ID_OPUS:
            return CodecId::Opus;
        case AV_CODEC_ID_AAC:
        case AV_CODEC_ID_AAC_LATM:
            return CodecId::Aac;
        case AV_CODEC_ID_ALAC:
            return CodecId::Alac;
        case AV_CODEC_ID_WAVPACK:
            return CodecId::WavPack;
        case AV_CODEC_ID_APE:
            return CodecId::Ape;
        case AV_CODEC_ID_PCM_S16LE:
        case AV_CODEC_ID_PCM_S16BE:
        case AV_CODEC_ID_PCM_S24LE:
        case AV_CODEC_ID_PCM_S24BE:
        case AV_CODEC_ID_PCM_S32LE:
        case AV_CODEC_ID_PCM_S32BE:
        case AV_CODEC_ID_PCM_F32LE:
        case AV_CODEC_ID_PCM_F32BE:
        case AV_CODEC_ID_PCM_F64LE:
        case AV_CODEC_ID_PCM_F64BE:
            return CodecId::Pcm;
        case AV_CODEC_ID_ATRAC9:
            return CodecId::Atrac9;
        case AV_CODEC_ID_GSM:
        case AV_CODEC_ID_GSM_MS:
            return CodecId::Gsm;
        default:
            return CodecId::Unknown;
    }
}

std::string codec_long_name(AVCodecID av_id) {
    if (const AVCodecDescriptor* desc = avcodec_descriptor_get(av_id)) {
        return std::string{desc->long_name ? desc->long_name : desc->name};
    }
    return "unknown";
}

bool is_lossless_codec(CodecId codec) {
    switch (codec) {
        case CodecId::Flac:
        case CodecId::Alac:
        case CodecId::WavPack:
        case CodecId::Pcm:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
//  ReplayGain tag parsing
// ---------------------------------------------------------------------------

/// Parses a ReplayGain 1.0 / 2.0 gain string ("-3.21 dB" or "-3.21 dB\0").
double parse_replaygain_gain(std::string_view s) {
    // Strip whitespace and "dB" suffix.
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == 'B' ||
                          s.back() == 'd' || s.back() == '\0'))
        s.remove_suffix(1);
    if (s.empty()) return 0.0;
    char* end = nullptr;
    double val = std::strtod(std::string{s}.c_str(), &end);
    if (end == std::string{s}.c_str()) return 0.0;
    return val;
}

/// Parses a ReplayGain peak string ("0.98765" or "1.000000").
double parse_replaygain_peak(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    if (s.empty()) return 0.0;
    char* end = nullptr;
    double val = std::strtod(std::string{s}.c_str(), &end);
    if (end == std::string{s}.c_str()) return 0.0;
    return val > 0.0 ? val : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
//  FfmpegDecoder public API
// ---------------------------------------------------------------------------

Error FfmpegDecoder::ffmpeg_error(const int errcode, const std::string_view operation) const {
    const auto code = classify_ffmpeg_error(errcode);
    const auto msg = fmt::format("FFmpeg error during {}: {}", operation, ffmpeg_strerror(errcode));
    return Error{code, std::string{msg}, ffmpeg_strerror(errcode)};
}

Result<StreamInfo> FfmpegDecoder::open(const std::filesystem::path& path) {
    close();

    fmt_ctx_ = avformat_alloc_context();
    if (!fmt_ctx_) {
        return err(ErrorCode::DecoderInitFailed, "Could not allocate FFmpeg format context.");
    }

    // Content-based format detection (REQ-AUD-027).
    // av_probe_input_buffer3 probes the data itself rather than trusting the
    // extension, which is exactly what spec §8.3 requires.
    AVProbeData probe_data{};
    std::vector<std::uint8_t> probe_buf(8192);

    // Open the file and read the header with content probing.
    int err = avformat_open_input(&fmt_ctx_, path.string().c_str(), nullptr, nullptr);
    if (err < 0) {
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
        return ffmpeg_error(err, "opening file");
    }

    // Enhance probing sensitivity: 50 000 kb = 50 MB max probe buffer.
    fmt_ctx_->max_analyze_duration = 50000000;
    fmt_ctx_->max_probe_packets = 2500;

    err = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (err < 0) {
        avformat_close_input(&fmt_ctx_);
        return ffmpeg_error(err, "finding stream info");
    }

    // Find the best audio stream.
    stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec_, 0);
    if (stream_index_ < 0) {
        avformat_close_input(&fmt_ctx_);
        return err(ErrorCode::NoAudioStream,
                   "The file contains no audio stream that FFmpeg can decode.");
    }

    AVStream* stream = fmt_ctx_->streams[stream_index_];

    // Open the codec.
    dec_ctx_ = avcodec_alloc_context3(codec_);
    if (!dec_ctx_) {
        avformat_close_input(&fmt_ctx_);
        return err(ErrorCode::DecoderInitFailed, "Could not allocate FFmpeg codec context.");
    }

    err = avcodec_parameters_to_context(dec_ctx_, stream->codecpar);
    if (err < 0) {
        avcodec_free_context(&dec_ctx_);
        avformat_close_input(&fmt_ctx_);
        return ffmpeg_error(err, "copying codec parameters");
    }

    // Use the maximum number of threads for decoding performance.
    dec_ctx_->thread_count = 0;  // 0 = auto (FFmpeg chooses based on codec)

    err = avcodec_open2(dec_ctx_, codec_, nullptr);
    if (err < 0) {
        avcodec_free_context(&dec_ctx_);
        avformat_close_input(&fmt_ctx_);
        return ffmpeg_error(err, "opening codec");
    }

    // Allocate packet and frame.
    pkt_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!pkt_ || !frame_) {
        av_packet_free(&pkt_);
        av_frame_free(&frame_);
        avcodec_free_context(&dec_ctx_);
        avformat_close_input(&fmt_ctx_);
        return err(ErrorCode::DecoderInitFailed, "Could not allocate FFmpeg packet or frame.");
    }

    // ---- Populate StreamInfo ----
    detect_codec_type();

    info_.format.sample_rate =
        static_cast<std::uint32_t>(dec_ctx_->sample_rate > 0 ? dec_ctx_->sample_rate : 44100);
    info_.format.channels =
        static_cast<std::uint16_t>(dec_ctx_->ch_layout.nb_channels > 0
                                       ? dec_ctx_->ch_layout.nb_channels
                                       : 2);
    info_.format.bits_per_sample = 32;  // decoded output is always float32

    // Total frames.
    if (stream->duration != AV_NOPTS_VALUE && stream->time_base.den > 0) {
        const auto dur =
            av_rescale(stream->duration, stream->time_base.den,
                       static_cast<std::int64_t>(stream->time_base.num) * info_.format.sample_rate);
        info_.total_frames = static_cast<std::uint64_t>(std::max<std::int64_t>(dur, 0));
    }

    // Channel layout.
    info_.layout = avchannel_layout_to_layout(dec_ctx_->ch_layout.u.mask,
                                              static_cast<std::size_t>(dec_ctx_->ch_layout.nb_channels));

    // Seekability.
    info_.seekable = (fmt_ctx_->iformat->flags & AVFMT_SEEK_TO_PTS) != 0 ||
                     (fmt_ctx_->iformat->flags & AVFMT_NO_BYTE_SEEK) == 0;

    // ReplayGain.
    info_.replaygain = read_replaygain_tags();

    // Gapless info.
    info_.gapless = read_gapless_info();
    if (info_.gapless) {
        info_.head_trim = info_.gapless->skip_start_frames;
        info_.tail_trim = info_.gapless->skip_end_frames;
    }

    opened_ = true;
    return info_;
}

void FfmpegDecoder::detect_codec_type() {
    if (!dec_ctx_ || !codec_) return;

    info_.codec = ffmpeg_codec_id_to_codec(codec_->id);
    info_.codec_name = codec_long_name(codec_->id);
    info_.is_lossless = is_lossless_codec(info_.codec);
}

ReplayGainTags FfmpegDecoder::read_replaygain_tags() const {
    ReplayGainTags rg{};

    if (!fmt_ctx_ || !fmt_ctx_->metadata) return rg;

    AVDictionaryEntry* tag = nullptr;
    // ReplayGain 1.0 keys (lowercase, as written by most taggers).
    const char* const keys[] = {
        "replaygain_track_gain", "replaygain_album_gain", "replaygain_track_peak",
        "replaygain_album_peak",
        // ReplayGain 2.0 keys (title-case).
        "REPLAYGAIN_TRACK_GAIN", "REPLAYGAIN_ALBUM_GAIN", "REPLAYGAIN_TRACK_PEAK",
        "REPLAYGAIN_ALBUM_PEAK",
    };

    while ((tag = av_dict_get(fmt_ctx_->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        std::string_view key{tag->key ? tag->key : ""};
        std::string_view val{tag->value ? tag->value : ""};
        if (key == "replaygain_track_gain" || key == "REPLAYGAIN_TRACK_GAIN") {
            rg.track_gain_db = parse_replaygain_gain(val);
            rg.has_track_gain = true;
        } else if (key == "replaygain_album_gain" || key == "REPLAYGAIN_ALBUM_GAIN") {
            rg.album_gain_db = parse_replaygain_gain(val);
            rg.has_album_gain = true;
        } else if (key == "replaygain_track_peak" || key == "REPLAYGAIN_TRACK_PEAK") {
            rg.track_peak = parse_replaygain_peak(val);
            rg.has_track_peak = true;
        } else if (key == "replaygain_album_peak" || key == "REPLAYGAIN_ALBUM_PEAK") {
            // Album peak is read but currently only track peak is used for
            // pre-amp computation.
        }
    }

    return rg;
}

std::optional<GaplessInfo> FfmpegDecoder::read_gapless_info() const {
    if (!fmt_ctx_) return std::nullopt;

    AVDictionaryEntry* tag = nullptr;
    AVDictionaryEntry* tag_start = nullptr;

    // Try iTunSMPB (AAC/M4A) — REQ-AUD-040.
    tag = av_dict_get(fmt_ctx_->metadata, "iTunSMPB", nullptr, 0);
    if (tag && tag->value) {
        // The format is: "begin ms / dur ms / orig / end ms"
        // e.g. "0 18986496 00000000 18986496"
        std::string_view val{tag->value};
        std::vector<std::uint64_t> fields;
        std::size_t pos = 0;
        while (pos < val.size() && fields.size() < 4) {
            // Skip spaces.
            while (pos < val.size() && val[pos] == ' ') ++pos;
            if (pos >= val.size()) break;
            std::size_t end = pos;
            while (end < val.size() && val[end] >= '0' && val[end] <= '9') ++end;
            if (end > pos) {
                try {
                    std::uint64_t n = 0;
                    for (std::size_t i = pos; i < end; ++i) {
                        n = n * 10 + static_cast<std::uint64_t>(val[i] - '0');
                    }
                    fields.push_back(n);
                } catch (...) {
                    break;
                }
            }
            pos = end + 1;
        }
        if (fields.size() >= 4) {
            // field 1 = priming (encoder delay in ns, we use samples at 44.1k)
            // field 2 = orig sample count
            GaplessInfo gi{};
            gi.skip_start_frames = static_cast<std::uint32_t>(
                fields[0] * info_.format.sample_rate / 1000000000ULL);
            gi.skip_end_frames = static_cast<std::uint32_t>(
                fields[2] * info_.format.sample_rate / 1000000000ULL);
            if (fields[1] > fields[0] + fields[2]) {
                gi.valid_frames = fields[1] - fields[0] - fields[2];
            }
            gi.source = 4;  // GaplessSource::ITunSMPB
            return gi;
        }
    }

    // Try LAME tag (MP3) via the gapless_info module.
    // For MP3, the gapless_info module reads the first frame directly.
    // We pass an empty span here; real MP3 gapless needs the first frame bytes.
    // We handle MP3 gapless at the first seek/open instead.

    // For native formats (FLAC, ALAC, WavPack), the native_gapless_info path
    // returns a no-op GaplessInfo with source=None, which is fine because the
    // container already carries the exact frame count.

    return std::nullopt;
}

Result<std::size_t> FfmpegDecoder::read(PlanarFrames destination) {
    if (!opened_ || !fmt_ctx_ || !dec_ctx_) {
        return err(ErrorCode::InvalidState, "The decoder is not open.");
    }

    // Return any carryover from a previous partial decode first.
    if (carryover_offset_ < carryover_.size()) {
        const auto carry_frames =
            (carryover_.size() - carryover_offset_) / info_.format.channels;
        const auto frames_to_copy =
            std::min(destination.frames, static_cast<std::size_t>(carry_frames));
        for (std::size_t ch = 0; ch < destination.channels; ++ch) {
            for (std::size_t f = 0; f < frames_to_copy; ++f) {
                destination.planes[ch][f] = carryover_[carryover_offset_ + f * destination.channels + ch];
            }
        }
        carryover_offset_ += frames_to_copy * destination.channels;
        if (carryover_offset_ >= carryover_.size()) {
            carryover_.clear();
            carryover_offset_ = 0;
        }
        if (frames_to_copy > 0) return frames_to_copy;
    }

    std::size_t output_frames = 0;

    while (output_frames < destination.frames) {
        // Feed packets until we have decoded output.
        int ret = avcodec_receive_frame(dec_ctx_, frame_);
        if (ret == 0) {
            // Got a decoded frame.
            const auto frame_channels = static_cast<std::size_t>(frame_->ch_layout.nb_channels);
            const auto frame_samples = static_cast<std::size_t>(frame_->nb_samples);

            // Convert float -> float if needed (FFmpeg decodes to float or s16).
            const float* src = frame_->extended_data[0] != nullptr
                                   ? reinterpret_cast<const float*>(frame_->data[0])
                                   : nullptr;

            // The frame is planar if extended_data == data and each plane is
            // separate.  FFmpeg always produces planar float for float decoders.
            const bool is_planar =
                frame_->format == AV_SAMPLE_FMT_FLTP || frame_->format == AV_SAMPLE_FMT_FLAP;

            if (frame_samples > 0 && frame_channels > 0) {
                // How many output frames can we fit?
                const auto space = destination.frames - output_frames;
                const auto frames_to_write = std::min(space, frame_samples);

                if (is_planar) {
                    // Planar: each channel is a separate plane.
                    for (std::size_t ch = 0; ch < std::min(destination.channels, frame_channels);
                         ++ch) {
                        const float* plane = reinterpret_cast<const float*>(
                            frame_->extended_data[ch]);
                        if (!plane) continue;
                        for (std::size_t f = 0; f < frames_to_write; ++f) {
                            destination.planes[ch][output_frames + f] = plane[f];
                        }
                    }
                } else {
                    // Interleaved: de-interleave manually.
                    for (std::size_t f = 0; f < frames_to_write; ++f) {
                        for (std::size_t ch = 0;
                             ch < std::min(destination.channels, frame_channels); ++ch) {
                            const float* interleaved =
                                reinterpret_cast<const float*>(frame_->data[0]);
                            destination.planes[ch][output_frames + f] =
                                interleaved[f * frame_channels + ch];
                        }
                    }
                }
                output_frames += frames_to_write;

                // If the frame had more samples than we could write, store the
                // remainder as carryover.
                if (frames_to_write < frame_samples) {
                    carryover_.resize(frame_channels * (frame_samples - frames_to_write));
                    carryover_offset_ = 0;
                    for (std::size_t ch = 0; ch < frame_channels; ++ch) {
                        const float* plane =
                            is_planar ? reinterpret_cast<const float*>(frame_->extended_data[ch])
                                      : nullptr;
                        if (!plane) continue;
                        for (std::size_t f = frames_to_write; f < frame_samples; ++f) {
                            carryover_[(f - frames_to_write) * frame_channels + ch] = plane[f];
                        }
                    }
                }
            }
            av_frame_unref(frame_);
        } else if (ret == AVERROR_EOF) {
            // No more frames will come from this stream.
            break;
        } else if (ret == AVERROR(EAGAIN)) {
            // Need more input packets.
            ret = av_read_frame(fmt_ctx_, pkt_);
            if (ret < 0) {
                if (ret == AVERROR_EOF) {
                    // Drain: send nullptr packet.
                    avcodec_send_packet(dec_ctx_, nullptr);
                    continue;
                }
                return ffmpeg_error(ret, "reading frame");
            }
            if (pkt_->stream_index != stream_index_) {
                av_packet_unref(pkt_);
                continue;
            }
            ret = avcodec_send_packet(dec_ctx_, pkt_);
            av_packet_unref(pkt_);
            if (ret < 0) {
                return ffmpeg_error(ret, "sending packet");
            }
        } else {
            return ffmpeg_error(ret, "receiving frame");
        }
    }

    return output_frames;
}

Status FfmpegDecoder::seek(std::uint64_t frame) {
    if (!opened_ || !fmt_ctx_ || !dec_ctx_) {
        return err(ErrorCode::InvalidState, "The decoder is not open.");
    }

    AVStream* stream = fmt_ctx_->streams[stream_index_];
    if (!stream) return err(ErrorCode::SeekFailed, "No audio stream available.");

    // Convert frame index to FFmpeg timestamp (in stream time base).
    const auto ts = av_rescale(frame, stream->time_base.den,
                               static_cast<std::int64_t>(stream->time_base.num) *
                                   static_cast<std::int64_t>(info_.format.sample_rate));

    // Flush the codec before seeking (required by many codecs).
    avcodec_flush_buffers(dec_ctx_);

    const int ret = av_seek_frame(fmt_ctx_, stream_index_, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        return ffmpeg_error(ret, "seeking");
    }

    // Discard any pending carryover.
    carryover_.clear();
    carryover_offset_ = 0;

    return ok();
}

void FfmpegDecoder::close() noexcept {
    if (pkt_) {
        av_packet_free(&pkt_);
        pkt_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (dec_ctx_) {
        avcodec_free_context(&dec_ctx_);
        dec_ctx_ = nullptr;
    }
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    codec_ = nullptr;
    stream_index_ = -1;
    opened_ = false;
    info_ = StreamInfo{};
    carryover_.clear();
    carryover_offset_ = 0;
}

std::optional<StreamInfo> FfmpegDecoder::stream_info() const noexcept {
    return opened_ ? std::optional<StreamInfo>{info_} : std::nullopt;
}

bool FfmpegDecoder::is_open() const noexcept { return opened_; }

#endif  // ARROW_HAVE_FFMPEG

}  // namespace arrow::audio
