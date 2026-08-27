// SPDX-License-Identifier: MPL-2.0
// Gapless trim metadata — spec §8.4 (REQ-AUD-035 .. REQ-AUD-045).
//
// Everything here is a pure byte-level parser: no FFmpeg, no file I/O. Each
// function takes a buffer and returns a Result. That makes the whole module
// unit-testable against synthetic inputs and directly fuzzable (§21.6 targets
// fuzz_xinglame and fuzz_mp4atoms), which matters because these parsers read
// attacker-controlled bytes and a wrong answer here is an audible defect.
//
// The spec's definition is exacting (REQ-AUD-035): concatenated output must be
// SAMPLE-IDENTICAL to the original continuous source. That leaves no room for
// "close enough", so every field is validated and a doubtful tag is rejected
// rather than trusted (REQ-AUD-039, REQ-AUD-042).

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/error.hpp"

namespace eclipse::audio {

/// Sentinel for "the exact frame count is not known".
inline constexpr std::uint64_t kUnknownFrames = ~std::uint64_t{0};

/// MPEG-1 Layer III decoder delay, in samples. REQ-AUD-037 fixes this at 529:
/// it is the group delay of the polyphase/MDCT filterbank that every compliant
/// decoder introduces, and it must be added to the encoder delay.
inline constexpr std::uint32_t kMp3DecoderDelay = 529;

/// Where the trim values came from. Surfaced in the track technical-info panel
/// (REQ-UIX-017) so the user can be told *why* a boundary is not gapless — the
/// spec explicitly requires that honesty (REQ-AUD-038).
enum class GaplessSource {
    None,      ///< no usable metadata; conservative defaults applied
    Native,    ///< format carries an exact frame count (FLAC, WavPack, WAV...)
    XingLame,  ///< MP3 Xing/Info frame with a LAME extension
    ITunSMPB,  ///< MP4/M4A iTunSMPB free-form tag
    OpusHead,  ///< Ogg Opus identification header pre-skip
    Granule,   ///< Ogg Vorbis granule position
};

[[nodiscard]] std::string_view to_string(GaplessSource s) noexcept;

/// The trim description for one stream. REQ-AUD-036.
struct GaplessInfo {
    std::uint32_t skip_start_frames = 0;
    std::uint32_t skip_end_frames = 0;
    std::uint64_t valid_frames = kUnknownFrames;
    GaplessSource source = GaplessSource::None;

    /// True when this stream can take part in a sample-exact splice, i.e. we
    /// have real metadata rather than a fallback guess.
    [[nodiscard]] bool supports_sample_exact_splice() const noexcept {
        return source != GaplessSource::None;
    }

    /// Frames actually played after trimming, or kUnknownFrames.
    [[nodiscard]] std::uint64_t playable_frames() const noexcept;

    friend bool operator==(const GaplessInfo&, const GaplessInfo&) = default;
};

/// True when a boundary between `outgoing` and `incoming` can be spliced with
/// zero inserted or dropped samples (REQ-AUD-046). Both sides need real
/// metadata; a `None` source on either side means we must fall back to the
/// boundary fade in REQ-AUD-049.
[[nodiscard]] bool can_splice_sample_exactly(const GaplessInfo& outgoing,
                                             const GaplessInfo& incoming) noexcept;

// ===========================================================================
//  MPEG audio frame header — needed to locate the Xing/Info frame
// ===========================================================================

enum class MpegVersion { Mpeg1, Mpeg2, Mpeg25 };

/// Decoded MPEG audio frame header.
struct MpegFrameHeader {
    MpegVersion version = MpegVersion::Mpeg1;
    int layer = 3;                   ///< 1, 2 or 3
    std::uint32_t bitrate_kbps = 0;  ///< 0 for the "free" bitrate index
    std::uint32_t sample_rate_hz = 0;
    int channels = 2;
    bool is_mono = false;
    bool has_crc = false;
    bool padded = false;
    std::uint32_t frame_bytes = 0;      ///< total frame length including header
    std::uint32_t side_info_bytes = 0;  ///< Layer III side information size
    /// Samples produced per frame: 1152 (MPEG-1 L3), 576 (MPEG-2/2.5 L3).
    std::uint32_t samples_per_frame = 1152;
};

/// Parses a 4-byte MPEG audio frame header at the start of `data`.
/// Rejects: short input, bad sync, reserved version, reserved layer, free or
/// reserved bitrate index, reserved sample-rate index.
[[nodiscard]] Result<MpegFrameHeader> parse_mpeg_frame_header(
    std::span<const std::uint8_t> data);

// ===========================================================================
//  MP3 — Xing/Info + LAME  (REQ-AUD-037, REQ-AUD-038, REQ-AUD-039)
// ===========================================================================

/// Raw contents of the Xing/Info tag, before the gapless formula is applied.
struct XingLameTag {
    bool is_info_magic = false;  ///< "Info" (CBR) rather than "Xing"
    bool has_frame_count = false;
    bool has_byte_count = false;
    bool has_toc = false;
    bool has_quality = false;
    std::uint32_t frame_count = 0;  ///< audio frames, excluding this one
    std::uint32_t byte_count = 0;

    bool has_lame = false;
    std::uint32_t encoder_delay = 0;    ///< 12-bit field
    std::uint32_t encoder_padding = 0;  ///< 12-bit field
    bool lame_crc_ok = false;
    char encoder[10] = {};  ///< 9 chars + NUL, e.g. "LAME3.100"
};

/// Parses the Xing/Info tag out of the first MPEG frame of an MP3 stream.
///
/// `data` must begin at the frame's sync word. The tag lives immediately after
/// the frame header plus (for Layer III) the side information, whose size
/// depends on version and channel mode — which is why this needs the header.
[[nodiscard]] Result<XingLameTag> parse_xing_lame(std::span<const std::uint8_t> data);

/// Applies REQ-AUD-037's formula to a parsed tag.
///
///     skip_start_frames = encoder_delay + 529
///     skip_end_frames   = max(0, encoder_padding - 529)
///
/// A tag whose LAME CRC failed is treated as absent (REQ-AUD-039), falling back
/// to REQ-AUD-038's conservative defaults.
[[nodiscard]] GaplessInfo gapless_from_xing_lame(const XingLameTag& tag,
                                                 std::uint32_t samples_per_frame);

/// Convenience: parse the first frame and produce GaplessInfo in one step.
/// Never fails — on any parse problem it returns the REQ-AUD-038 fallback
/// (`skip_start = 529`, `source = None`), because an unparseable Xing tag must
/// not stop the file from playing.
[[nodiscard]] GaplessInfo mp3_gapless_info(std::span<const std::uint8_t> first_frame);

// ===========================================================================
//  MP4 / M4A — iTunSMPB  (REQ-AUD-040, REQ-AUD-041, REQ-AUD-042)
// ===========================================================================

/// Parses an `iTunSMPB` free-form tag value.
///
/// The value is a space-separated run of hex fields. Per REQ-AUD-040 we use:
///   field 2 -> priming / encoder delay -> skip_start_frames
///   field 3 -> remainder / padding     -> skip_end_frames
///   field 4 -> original sample count   -> valid_frames
///
/// `total_frames_hint` lets the parser sanity-check the tag against the actual
/// stream length; pass kUnknownFrames to skip that check.
///
/// REQ-AUD-042 governs the failure behaviour: a wrong field count, non-hex
/// characters, or values exceeding the frame count cause outright rejection.
/// The parser never produces a negative or overflowing skip.
[[nodiscard]] Result<GaplessInfo> parse_itunsmpb(
    std::string_view value, std::uint64_t total_frames_hint = kUnknownFrames);

/// AAC-LC decoder priming used when no iTunSMPB tag exists (REQ-AUD-041).
inline constexpr std::uint32_t kAacDefaultPriming = 1024;

/// REQ-AUD-041 fallback: no iTunSMPB, so use the decoder-reported priming and
/// record that this is not authoritative.
[[nodiscard]] GaplessInfo aac_fallback_gapless_info(
    std::uint32_t priming_frames = kAacDefaultPriming,
    std::uint64_t total_frames = kUnknownFrames);

// ===========================================================================
//  Opus — OpusHead  (REQ-AUD-043)
// ===========================================================================

/// Decoded OpusHead identification packet (RFC 7845 §5.1).
struct OpusHead {
    std::uint8_t version = 1;
    std::uint8_t channel_count = 2;
    std::uint16_t pre_skip = 0;  ///< in 48 kHz samples
    std::uint32_t input_sample_rate_hz = 48000;
    std::int16_t output_gain_q7_8 = 0;  ///< Q7.8 dB
    std::uint8_t channel_mapping = 0;

    /// output_gain converted to decibels. RFC 7845 requires this be applied
    /// independently of, and in addition to, ReplayGain (REQ-AUD-043).
    [[nodiscard]] double output_gain_db() const noexcept;
};

/// Parses an OpusHead packet. Rejects wrong magic, an unsupported version, a
/// truncated packet, or a zero channel count.
[[nodiscard]] Result<OpusHead> parse_opus_head(std::span<const std::uint8_t> data);

/// Builds GaplessInfo from a parsed OpusHead.
///
/// pre_skip is defined in 48 kHz samples. When the decoder emits at a different
/// rate, `output_rate_hz` rescales it; pass 48000 for the native case.
[[nodiscard]] GaplessInfo gapless_from_opus_head(const OpusHead& head,
                                                 std::uint64_t total_frames = kUnknownFrames,
                                                 std::uint32_t output_rate_hz = 48000);

// ===========================================================================
//  Native exact-length formats  (REQ-AUD-044)
// ===========================================================================

/// FLAC, WavPack, APE, WAV, ALAC and friends carry an exact frame count, so
/// there is nothing to trim.
[[nodiscard]] GaplessInfo native_gapless_info(std::uint64_t total_frames) noexcept;

// ===========================================================================
//  Ogg Vorbis granule  (REQ-AUD-045)
// ===========================================================================

/// Derives trim from Ogg granule positions.
///
/// `final_granule` is the granule position of the last page. A negative
/// `initial_granule` means the encoder trimmed the start, and REQ-AUD-045
/// requires we honour it as a head skip.
[[nodiscard]] Result<GaplessInfo> gapless_from_granule(std::int64_t final_granule,
                                                       std::int64_t initial_granule = 0);

}  // namespace eclipse::audio
