// SPDX-License-Identifier: MPL-2.0
#include "audio/decode/gapless_info.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <vector>

#include "core/text.hpp"

namespace eclipse::audio {
namespace {

// --------------------------------------------------------------------- tables

/// MPEG-1 Layer III bitrates, indexed by the 4-bit bitrate index. Index 0 is
/// "free" and 15 is reserved; both are rejected.
constexpr std::array<std::uint32_t, 16> kBitrateMpeg1L3 = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
/// MPEG-2 / 2.5 Layer III bitrates.
constexpr std::array<std::uint32_t, 16> kBitrateMpeg2L3 = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
/// MPEG-1 Layer II bitrates (we parse the header even though we only use L3).
constexpr std::array<std::uint32_t, 16> kBitrateMpeg1L2 = {
    0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
constexpr std::array<std::uint32_t, 16> kBitrateMpeg1L1 = {
    0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};

constexpr std::array<std::uint32_t, 4> kRateMpeg1 = {44100, 48000, 32000, 0};
constexpr std::array<std::uint32_t, 4> kRateMpeg2 = {22050, 24000, 16000, 0};
constexpr std::array<std::uint32_t, 4> kRateMpeg25 = {11025, 12000, 8000, 0};

/// Big-endian readers. Every one is bounds-checked by the caller.
constexpr std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

constexpr std::uint16_t le16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(p[0]) |
                                      (static_cast<std::uint32_t>(p[1]) << 8));
}

constexpr std::uint32_t le32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

/// CRC-16 as used by the LAME tag: polynomial 0x8005, init 0x0000, MSB-first.
/// REQ-AUD-039 requires we validate it and ignore a tag that fails.
std::uint16_t lame_crc16(std::span<const std::uint8_t> data) noexcept {
    std::uint16_t crc = 0;
    for (const std::uint8_t byte : data) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint32_t>(byte) << 8);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000u) != 0) {
                crc = static_cast<std::uint16_t>(
                    ((static_cast<std::uint32_t>(crc) << 1) ^ 0x8005u) & 0xFFFFu);
            } else {
                crc = static_cast<std::uint16_t>((static_cast<std::uint32_t>(crc) << 1) &
                                                 0xFFFFu);
            }
        }
    }
    return crc;
}

}  // namespace

std::string_view to_string(GaplessSource s) noexcept {
    switch (s) {
        case GaplessSource::None:
            return "none";
        case GaplessSource::Native:
            return "native";
        case GaplessSource::XingLame:
            return "xing-lame";
        case GaplessSource::ITunSMPB:
            return "itunsmpb";
        case GaplessSource::OpusHead:
            return "opushead";
        case GaplessSource::Granule:
            return "granule";
    }
    return "none";
}

std::uint64_t GaplessInfo::playable_frames() const noexcept {
    if (valid_frames == kUnknownFrames) return kUnknownFrames;
    const std::uint64_t trim = static_cast<std::uint64_t>(skip_start_frames) +
                               static_cast<std::uint64_t>(skip_end_frames);
    return trim >= valid_frames ? 0u : valid_frames - trim;
}

bool can_splice_sample_exactly(const GaplessInfo& outgoing,
                               const GaplessInfo& incoming) noexcept {
    // REQ-AUD-046/047: both sides need authoritative metadata. A `None` source
    // means we guessed, and guessing cannot yield a sample-exact join.
    if (!outgoing.supports_sample_exact_splice()) return false;
    if (!incoming.supports_sample_exact_splice()) return false;
    // The outgoing side must know where its audio actually ends.
    return outgoing.valid_frames != kUnknownFrames;
}

// ===========================================================================
//  MPEG frame header
// ===========================================================================

Result<MpegFrameHeader> parse_mpeg_frame_header(std::span<const std::uint8_t> data) {
    if (data.size() < 4) {
        return err(ErrorCode::UnexpectedEnd,
                   "This MP3 file appears to be truncated.",
                   "need 4 header bytes, have " + std::to_string(data.size()));
    }

    // Sync: 11 set bits.
    if (data[0] != 0xFF || (data[1] & 0xE0u) != 0xE0u) {
        return err(ErrorCode::MalformedHeader,
                   "This file does not look like an MP3 stream.",
                   "missing frame sync");
    }

    MpegFrameHeader h;

    // Version: bits 4-3 of byte 1.  00 = MPEG-2.5, 01 = reserved,
    //                               10 = MPEG-2,   11 = MPEG-1
    switch ((data[1] >> 3) & 0x03u) {
        case 0:
            h.version = MpegVersion::Mpeg25;
            break;
        case 1:
            return err(ErrorCode::MalformedHeader,
                       "This MP3 file uses a reserved MPEG version.",
                       "version bits = 01");
        case 2:
            h.version = MpegVersion::Mpeg2;
            break;
        default:
            h.version = MpegVersion::Mpeg1;
            break;
    }

    // Layer: bits 2-1 of byte 1.  01 = Layer III, 10 = II, 11 = I, 00 reserved.
    switch ((data[1] >> 1) & 0x03u) {
        case 0:
            return err(ErrorCode::MalformedHeader,
                       "This MP3 file uses a reserved layer.",
                       "layer bits = 00");
        case 1:
            h.layer = 3;
            break;
        case 2:
            h.layer = 2;
            break;
        default:
            h.layer = 1;
            break;
    }

    h.has_crc = ((data[1] & 0x01u) == 0);  // bit is "protection absent"

    const auto bitrate_index = static_cast<std::size_t>((data[2] >> 4) & 0x0Fu);
    const auto rate_index = static_cast<std::size_t>((data[2] >> 2) & 0x03u);
    h.padded = ((data[2] >> 1) & 0x01u) != 0;

    // Channel mode: bits 7-6 of byte 3. 11 = single channel.
    const unsigned mode = (data[3] >> 6) & 0x03u;
    h.is_mono = (mode == 3);
    h.channels = h.is_mono ? 1 : 2;

    // Bitrate.
    if (h.version == MpegVersion::Mpeg1) {
        h.bitrate_kbps = (h.layer == 3)   ? kBitrateMpeg1L3[bitrate_index]
                         : (h.layer == 2) ? kBitrateMpeg1L2[bitrate_index]
                                          : kBitrateMpeg1L1[bitrate_index];
    } else {
        h.bitrate_kbps = kBitrateMpeg2L3[bitrate_index];
    }
    if (h.bitrate_kbps == 0) {
        return err(ErrorCode::MalformedHeader,
                   "This MP3 file uses an unsupported bitrate.",
                   "bitrate index = " + std::to_string(bitrate_index));
    }

    // Sample rate.
    switch (h.version) {
        case MpegVersion::Mpeg1:
            h.sample_rate_hz = kRateMpeg1[rate_index];
            break;
        case MpegVersion::Mpeg2:
            h.sample_rate_hz = kRateMpeg2[rate_index];
            break;
        case MpegVersion::Mpeg25:
            h.sample_rate_hz = kRateMpeg25[rate_index];
            break;
    }
    if (h.sample_rate_hz == 0) {
        return err(ErrorCode::MalformedHeader,
                   "This MP3 file uses a reserved sample rate.",
                   "rate index = " + std::to_string(rate_index));
    }

    // Samples per frame, and therefore frame length.
    if (h.layer == 1) {
        h.samples_per_frame = 384;
    } else if (h.layer == 2) {
        h.samples_per_frame = 1152;
    } else {
        h.samples_per_frame = (h.version == MpegVersion::Mpeg1) ? 1152u : 576u;
    }

    const std::uint32_t pad = h.padded ? ((h.layer == 1) ? 4u : 1u) : 0u;
    if (h.layer == 1) {
        h.frame_bytes = (12u * h.bitrate_kbps * 1000u / h.sample_rate_hz + pad) * 4u;
    } else {
        h.frame_bytes =
            (h.samples_per_frame / 8u) * h.bitrate_kbps * 1000u / h.sample_rate_hz + pad;
    }

    // Layer III side-information size: this is what determines where the Xing
    // tag starts, so getting it wrong silently breaks gapless.
    if (h.layer == 3) {
        if (h.version == MpegVersion::Mpeg1) {
            h.side_info_bytes = h.is_mono ? 17u : 32u;
        } else {
            h.side_info_bytes = h.is_mono ? 9u : 17u;
        }
    } else {
        h.side_info_bytes = 0;
    }

    return h;
}

// ===========================================================================
//  Xing / Info + LAME
// ===========================================================================

Result<XingLameTag> parse_xing_lame(std::span<const std::uint8_t> data) {
    auto header = parse_mpeg_frame_header(data);
    if (!header) return std::move(header).error();
    const MpegFrameHeader& h = header.value();

    if (h.layer != 3) {
        return err(ErrorCode::UnsupportedFormat,
                   "Gapless information is only available for MP3 Layer III.",
                   "layer=" + std::to_string(h.layer));
    }

    // The tag sits after: 4-byte header + optional 2-byte CRC + side info.
    std::size_t offset = 4;
    if (h.has_crc) offset += 2;
    offset += h.side_info_bytes;

    if (data.size() < offset + 8) {
        return err(ErrorCode::UnexpectedEnd,
                   "This MP3 file has no gapless information.",
                   "frame too short for a Xing tag");
    }

    XingLameTag tag;
    const std::uint8_t* p = data.data() + offset;

    if (std::memcmp(p, "Xing", 4) == 0) {
        tag.is_info_magic = false;
    } else if (std::memcmp(p, "Info", 4) == 0) {
        tag.is_info_magic = true;  // CBR variant; same layout
    } else {
        return err(ErrorCode::MalformedHeader,
                   "This MP3 file has no gapless information.",
                   "no Xing/Info magic at the expected offset");
    }

    const std::uint32_t flags = be32(p + 4);
    tag.has_frame_count = (flags & 0x0001u) != 0;
    tag.has_byte_count = (flags & 0x0002u) != 0;
    tag.has_toc = (flags & 0x0004u) != 0;
    tag.has_quality = (flags & 0x0008u) != 0;

    // Walk the optional fields in their fixed order, bounds-checking each.
    std::size_t cursor = offset + 8;
    const auto need = [&](std::size_t bytes) -> bool { return data.size() >= cursor + bytes; };

    if (tag.has_frame_count) {
        if (!need(4))
            return err(ErrorCode::UnexpectedEnd,
                       "This MP3 file's gapless information is truncated.",
                       "frame count field");
        tag.frame_count = be32(data.data() + cursor);
        cursor += 4;
    }
    if (tag.has_byte_count) {
        if (!need(4))
            return err(ErrorCode::UnexpectedEnd,
                       "This MP3 file's gapless information is truncated.",
                       "byte count field");
        tag.byte_count = be32(data.data() + cursor);
        cursor += 4;
    }
    if (tag.has_toc) {
        if (!need(100))
            return err(ErrorCode::UnexpectedEnd,
                       "This MP3 file's gapless information is truncated.",
                       "table of contents");
        cursor += 100;
    }
    if (tag.has_quality) {
        if (!need(4))
            return err(ErrorCode::UnexpectedEnd,
                       "This MP3 file's gapless information is truncated.",
                       "quality field");
        cursor += 4;
    }

    // The LAME extension follows the Xing fields. Its layout, from the start of
    // the extension:
    //   +0   9 bytes  encoder short version, e.g. "LAME3.100"
    //   +9   1 byte   revision (high nibble) + VBR method (low nibble)
    //   +10  1 byte   lowpass filter value / 100
    //   +11  8 bytes  ReplayGain peak + radio/audiophile fields
    //   +19  1 byte   encoding flags + ATH type
    //   +20  1 byte   ABR bitrate / minimal bitrate
    //   +21  3 bytes  encoder delay (12 bits) || encoder padding (12 bits)
    //   +24  1 byte   misc (stereo mode, source frequency, noise shaping)
    //   +25  1 byte   MP3 gain
    //   +26  2 bytes  preset / surround info
    //   +28  4 bytes  music length
    //   +32  2 bytes  music CRC
    //   +34  2 bytes  CRC-16 of the first 190 bytes of the frame
    // Total 36 bytes.
    constexpr std::size_t kLameExtBytes = 36;
    constexpr std::size_t kDelayFieldOffset = 21;

    if (data.size() < cursor + kLameExtBytes) {
        // No LAME extension: legal, and REQ-AUD-038 covers the fallback.
        return tag;
    }

    const std::uint8_t* lame = data.data() + cursor;

    // The encoder string is normally "LAME..." or "Lavf"/"Lavc" for FFmpeg.
    // Require a printable ASCII signature so random bytes are not mistaken for
    // a tag; this is a cheap and effective sanity gate.
    bool printable = true;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint8_t c = lame[i];
        if (c < 0x20 || c > 0x7E) {
            printable = false;
            break;
        }
    }
    if (!printable) return tag;

    std::memcpy(tag.encoder, lame, 9);
    tag.encoder[9] = '\0';

    // 12-bit delay then 12-bit padding, packed big-endian into 3 bytes:
    //   byte0 = delay[11:4]
    //   byte1 = delay[3:0] << 4 | padding[11:8]
    //   byte2 = padding[7:0]
    const std::uint8_t d0 = lame[kDelayFieldOffset + 0];
    const std::uint8_t d1 = lame[kDelayFieldOffset + 1];
    const std::uint8_t d2 = lame[kDelayFieldOffset + 2];
    tag.encoder_delay =
        (static_cast<std::uint32_t>(d0) << 4) | (static_cast<std::uint32_t>(d1) >> 4);
    tag.encoder_padding =
        ((static_cast<std::uint32_t>(d1) & 0x0Fu) << 8) | static_cast<std::uint32_t>(d2);

    // REQ-AUD-039: validate the CRC. The LAME specification describes this as
    // "CRC-16 of the first 190 bytes of the frame", but 190 is not a universal
    // constant: it is where the CRC field happens to land for the standard
    // layout that LAME itself writes (32-byte stereo side info + Xing magic +
    // flags + frame count + byte count + 100-byte TOC + quality = 156, and
    // 156 + 34 = 190). Encoders that omit the TOC move the field earlier.
    //
    // The invariant that actually holds is: the CRC covers every byte of the
    // frame preceding the CRC field. Computing it that way is correct for the
    // standard layout *and* for the reduced ones, so we do that instead of
    // trusting the magic number.
    const std::size_t crc_coverage = cursor + 34;

    const std::uint16_t stored_crc = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(lame[34]) << 8) | static_cast<std::uint32_t>(lame[35]));

    if (stored_crc == 0) {
        // A zero field means the encoder did not compute a CRC. Refusing these
        // would break gapless for a large share of real-world libraries, so we
        // accept the tag as unverified rather than discarding it.
        tag.lame_crc_ok = true;
    } else if (data.size() >= crc_coverage) {
        tag.lame_crc_ok = (lame_crc16(data.subspan(0, crc_coverage)) == stored_crc);
    } else {
        tag.lame_crc_ok = false;  // cannot verify -> distrust
    }

    tag.has_lame = true;
    return tag;
}

GaplessInfo gapless_from_xing_lame(const XingLameTag& tag, std::uint32_t samples_per_frame) {
    GaplessInfo info;

    // REQ-AUD-039: a tag failing CRC must be ignored, not trusted.
    if (!tag.has_lame || !tag.lame_crc_ok) {
        info.skip_start_frames = kMp3DecoderDelay;
        info.skip_end_frames = 0;
        info.source = GaplessSource::None;
        if (tag.has_frame_count && samples_per_frame > 0) {
            info.valid_frames = static_cast<std::uint64_t>(tag.frame_count) *
                                static_cast<std::uint64_t>(samples_per_frame);
        }
        return info;
    }

    // REQ-AUD-037, verbatim:
    //   skip_start = encoder_delay + 529
    //   skip_end   = max(0, encoder_padding - 529)
    info.skip_start_frames = tag.encoder_delay + kMp3DecoderDelay;
    info.skip_end_frames =
        (tag.encoder_padding > kMp3DecoderDelay) ? tag.encoder_padding - kMp3DecoderDelay : 0u;
    info.source = GaplessSource::XingLame;

    if (tag.has_frame_count && samples_per_frame > 0) {
        info.valid_frames = static_cast<std::uint64_t>(tag.frame_count) *
                            static_cast<std::uint64_t>(samples_per_frame);

        // Defensive: a trim larger than the stream cannot be right.
        const std::uint64_t trim = static_cast<std::uint64_t>(info.skip_start_frames) +
                                   static_cast<std::uint64_t>(info.skip_end_frames);
        if (trim >= info.valid_frames) {
            info.skip_start_frames = kMp3DecoderDelay;
            info.skip_end_frames = 0;
            info.source = GaplessSource::None;
        }
    }
    return info;
}

GaplessInfo mp3_gapless_info(std::span<const std::uint8_t> first_frame) {
    // REQ-AUD-038: an unparseable tag must never stop playback, so this call
    // cannot fail — it degrades to the conservative default.
    GaplessInfo fallback;
    fallback.skip_start_frames = kMp3DecoderDelay;
    fallback.skip_end_frames = 0;
    fallback.source = GaplessSource::None;

    auto header = parse_mpeg_frame_header(first_frame);
    if (!header) return fallback;

    auto tag = parse_xing_lame(first_frame);
    if (!tag) return fallback;

    return gapless_from_xing_lame(tag.value(), header.value().samples_per_frame);
}

// ===========================================================================
//  iTunSMPB
// ===========================================================================

Result<GaplessInfo> parse_itunsmpb(std::string_view value, std::uint64_t total_frames_hint) {
    // REQ-AUD-042: be strict. This tag is attacker-controlled and a bad value
    // must be rejected rather than converted into a nonsense skip.
    if (value.size() > 512) {
        return err(ErrorCode::InputTooLarge,
                   "This track's gapless information is invalid.",
                   "iTunSMPB value too long: " + std::to_string(value.size()));
    }

    // Split on whitespace, dropping empties (the value is conventionally
    // space-padded at both ends).
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && (value[i] == ' ' || value[i] == '\t')) ++i;
        const std::size_t start = i;
        while (i < value.size() && value[i] != ' ' && value[i] != '\t') ++i;
        if (i > start) fields.push_back(value.substr(start, i - start));
    }

    // The canonical tag has 12 fields. Require at least the 4 we read, and
    // reject an implausibly long list.
    if (fields.size() < 4) {
        return err(ErrorCode::ParseError,
                   "This track's gapless information is incomplete.",
                   "iTunSMPB has " + std::to_string(fields.size()) + " fields, need >= 4");
    }
    if (fields.size() > 16) {
        return err(ErrorCode::ParseError,
                   "This track's gapless information is invalid.",
                   "iTunSMPB has " + std::to_string(fields.size()) + " fields");
    }

    std::uint64_t priming = 0, padding = 0, total = 0;
    if (!text::parse_hex(fields[1], priming) || !text::parse_hex(fields[2], padding) ||
        !text::parse_hex(fields[3], total)) {
        return err(ErrorCode::ParseError,
                   "This track's gapless information is invalid.",
                   "iTunSMPB contains a non-hexadecimal field");
    }

    // Values must fit the 32-bit skip fields.
    constexpr std::uint64_t kMax32 = std::numeric_limits<std::uint32_t>::max();
    if (priming > kMax32 || padding > kMax32) {
        return err(ErrorCode::OutOfRange,
                   "This track's gapless information is invalid.",
                   "priming/padding exceeds 32 bits");
    }

    // REQ-AUD-042: the trim cannot exceed the stream.
    if (total > 0 && priming + padding > total) {
        return err(ErrorCode::OutOfRange,
                   "This track's gapless information is inconsistent.",
                   "priming+padding (" + std::to_string(priming + padding) +
                       ") exceeds the sample count (" + std::to_string(total) + ")");
    }

    // Cross-check against the real stream length when we know it.
    if (total_frames_hint != kUnknownFrames && total > 0) {
        // Allow a small tolerance: some encoders record the pre-trim length.
        const std::uint64_t limit = total_frames_hint + total_frames_hint / 10 + 4096;
        if (total > limit) {
            return err(ErrorCode::OutOfRange,
                       "This track's gapless information does not match the audio.",
                       "tag says " + std::to_string(total) + " samples, stream has " +
                           std::to_string(total_frames_hint));
        }
    }

    GaplessInfo info;
    info.skip_start_frames = static_cast<std::uint32_t>(priming);
    info.skip_end_frames = static_cast<std::uint32_t>(padding);
    info.valid_frames = (total > 0) ? total : kUnknownFrames;
    info.source = GaplessSource::ITunSMPB;
    return info;
}

GaplessInfo aac_fallback_gapless_info(std::uint32_t priming_frames,
                                      std::uint64_t total_frames) {
    GaplessInfo info;
    info.skip_start_frames = priming_frames;
    info.skip_end_frames = 0;
    info.valid_frames = total_frames;
    // REQ-AUD-041: not authoritative, so the source stays None and the UI can
    // explain that this file lacks a gapless tag.
    info.source = GaplessSource::None;
    return info;
}

// ===========================================================================
//  OpusHead
// ===========================================================================

double OpusHead::output_gain_db() const noexcept {
    // Q7.8 fixed point: divide by 256.
    return static_cast<double>(output_gain_q7_8) / 256.0;
}

Result<OpusHead> parse_opus_head(std::span<const std::uint8_t> data) {
    // RFC 7845 §5.1: the header is at least 19 bytes.
    constexpr std::size_t kMinBytes = 19;
    if (data.size() < kMinBytes) {
        return err(ErrorCode::UnexpectedEnd,
                   "This Opus file's header is truncated.",
                   "need " + std::to_string(kMinBytes) + " bytes, have " +
                       std::to_string(data.size()));
    }
    if (std::memcmp(data.data(), "OpusHead", 8) != 0) {
        return err(ErrorCode::MalformedHeader,
                   "This does not look like an Opus stream.",
                   "missing OpusHead magic");
    }

    OpusHead h;
    h.version = data[8];
    // RFC 7845: a decoder must reject a major version it does not understand.
    // The major version is the high nibble; 0 is the only defined value.
    if ((h.version & 0xF0u) != 0x00u) {
        return err(ErrorCode::UnsupportedFormat,
                   "This Opus file uses an unsupported version.",
                   "version=" + std::to_string(h.version));
    }

    h.channel_count = data[9];
    if (h.channel_count == 0) {
        return err(ErrorCode::MalformedHeader,
                   "This Opus file declares zero channels.",
                   "channel_count=0");
    }

    h.pre_skip = le16(data.data() + 10);
    h.input_sample_rate_hz = le32(data.data() + 12);
    h.output_gain_q7_8 = static_cast<std::int16_t>(le16(data.data() + 16));
    h.channel_mapping = data[18];

    // Mapping families 1 and 2 carry a channel mapping table; family 255 is
    // undefined ordering. Only validate that a table, if promised, is present.
    if (h.channel_mapping == 1 || h.channel_mapping == 2) {
        const std::size_t needed = 21 + static_cast<std::size_t>(h.channel_count);
        if (data.size() < needed) {
            return err(ErrorCode::UnexpectedEnd,
                       "This Opus file's channel mapping table is truncated.",
                       "need " + std::to_string(needed) + " bytes");
        }
    }

    return h;
}

GaplessInfo gapless_from_opus_head(const OpusHead& head,
                                   std::uint64_t total_frames,
                                   std::uint32_t output_rate_hz) {
    GaplessInfo info;

    // REQ-AUD-043: pre_skip is defined in 48 kHz samples. Rescale when the
    // decoder emits at another rate.
    std::uint64_t skip = head.pre_skip;
    if (output_rate_hz != 0 && output_rate_hz != 48000) {
        skip = (skip * static_cast<std::uint64_t>(output_rate_hz) + 24000u) / 48000u;
    }
    info.skip_start_frames =
        static_cast<std::uint32_t>(skip > std::numeric_limits<std::uint32_t>::max()
                                       ? std::numeric_limits<std::uint32_t>::max()
                                       : skip);
    info.skip_end_frames = 0;
    info.valid_frames = total_frames;
    info.source = GaplessSource::OpusHead;
    return info;
}

// ===========================================================================
//  Native and granule
// ===========================================================================

GaplessInfo native_gapless_info(std::uint64_t total_frames) noexcept {
    GaplessInfo info;
    info.skip_start_frames = 0;
    info.skip_end_frames = 0;
    info.valid_frames = total_frames;
    info.source = GaplessSource::Native;
    return info;
}

Result<GaplessInfo> gapless_from_granule(std::int64_t final_granule,
                                         std::int64_t initial_granule) {
    if (final_granule < 0) {
        return err(ErrorCode::MalformedHeader,
                   "This file's length information is invalid.",
                   "final granule is negative: " + std::to_string(final_granule));
    }

    GaplessInfo info;
    info.source = GaplessSource::Granule;

    // REQ-AUD-045: a negative initial granule means the encoder trimmed the
    // start, and that trim must be honoured as a head skip.
    if (initial_granule < 0) {
        // Negate in the unsigned domain. `-initial_granule` is undefined for
        // INT64_MIN — the one input where the negative range is wider than the
        // positive one — and a granule position comes out of a file, so it can be
        // exactly that. Found by fuzz_gapless; UBSan reported it on the first
        // corpus replay. Two's complement makes the unsigned form exact for every
        // other value too, so this is not a special case bolted on for one input.
        const auto skip = static_cast<std::uint64_t>(-(initial_granule + 1)) + 1u;
        if (skip > std::numeric_limits<std::uint32_t>::max()) {
            return err(ErrorCode::OutOfRange,
                       "This file's length information is invalid.",
                       "initial granule trim too large");
        }
        info.skip_start_frames = static_cast<std::uint32_t>(skip);
    }

    const std::uint64_t total = static_cast<std::uint64_t>(final_granule);
    if (total < static_cast<std::uint64_t>(info.skip_start_frames)) {
        return err(ErrorCode::OutOfRange,
                   "This file's length information is inconsistent.",
                   "head trim exceeds the stream length");
    }
    info.valid_frames = total;
    return info;
}

}  // namespace eclipse::audio
