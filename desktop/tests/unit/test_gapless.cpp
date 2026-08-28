// SPDX-License-Identifier: MPL-2.0
// Tests for gapless trim metadata — spec §8.4.
//
// These tests build synthetic byte buffers rather than relying on sample files,
// so the exact field values under test are known by construction. The spec's
// requirement is sample-exactness (REQ-AUD-035), so every arithmetic result is
// asserted exactly, never approximately.

#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "audio/decode/gapless_info.hpp"

#include <gtest/gtest.h>

using namespace arrow;
using namespace arrow::audio;

namespace {

/// CRC-16/8005, MSB-first, init 0 — mirrors the implementation so the test can
/// build a buffer whose stored CRC is correct.
std::uint16_t crc16(const std::uint8_t* data, std::size_t n) {
    std::uint16_t crc = 0;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint32_t>(data[i]) << 8);
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u)
                      ? static_cast<std::uint16_t>(
                            ((static_cast<std::uint32_t>(crc) << 1) ^ 0x8005u) & 0xFFFFu)
                      : static_cast<std::uint16_t>((static_cast<std::uint32_t>(crc) << 1) &
                                                   0xFFFFu);
        }
    }
    return crc;
}

struct Mp3Options {
    std::uint32_t delay = 576;
    std::uint32_t padding = 1800;
    std::uint32_t frame_count = 10000;
    bool use_info_magic = false;
    bool include_lame = true;
    bool valid_crc = true;
    bool zero_crc = false;  ///< encoder did not compute a CRC
    bool mono = false;
    bool include_toc = false;
    const char* encoder = "LAME3.100";
};

/// Builds a plausible MPEG-1 Layer III frame containing a Xing/Info tag and,
/// optionally, a LAME extension with the requested delay/padding.
std::vector<std::uint8_t> make_mp3_frame(const Mp3Options& o) {
    std::vector<std::uint8_t> f(1024, 0);

    // Header: MPEG-1, Layer III, protection absent, 128 kbps, 44.1 kHz.
    f[0] = 0xFF;
    f[1] = 0xFB;                                             // 1111 1011 -> MPEG1, L3, no CRC
    f[2] = 0x90;                                             // bitrate idx 9 (128k), rate idx 0
    f[3] = static_cast<std::uint8_t>(o.mono ? 0xC0 : 0x00);  // 11 = mono, 00 = stereo

    const std::size_t side_info = o.mono ? 17u : 32u;
    std::size_t at = 4 + side_info;

    std::memcpy(f.data() + at, o.use_info_magic ? "Info" : "Xing", 4);
    at += 4;

    // Flags: frame count (+ optional TOC).
    const std::uint32_t flags = 0x0001u | (o.include_toc ? 0x0004u : 0u);
    f[at + 0] = static_cast<std::uint8_t>((flags >> 24) & 0xFFu);
    f[at + 1] = static_cast<std::uint8_t>((flags >> 16) & 0xFFu);
    f[at + 2] = static_cast<std::uint8_t>((flags >> 8) & 0xFFu);
    f[at + 3] = static_cast<std::uint8_t>(flags & 0xFFu);
    at += 4;

    f[at + 0] = static_cast<std::uint8_t>((o.frame_count >> 24) & 0xFFu);
    f[at + 1] = static_cast<std::uint8_t>((o.frame_count >> 16) & 0xFFu);
    f[at + 2] = static_cast<std::uint8_t>((o.frame_count >> 8) & 0xFFu);
    f[at + 3] = static_cast<std::uint8_t>(o.frame_count & 0xFFu);
    at += 4;

    if (o.include_toc) {
        for (std::size_t i = 0; i < 100; ++i) {
            f[at + i] = static_cast<std::uint8_t>(i * 2);
        }
        at += 100;
    }

    if (o.include_lame) {
        const std::size_t lame = at;
        std::memcpy(f.data() + lame, o.encoder, std::strlen(o.encoder));

        // Delay/padding packed big-endian: 12 bits each across 3 bytes at +21.
        f[lame + 21] = static_cast<std::uint8_t>((o.delay >> 4) & 0xFFu);
        f[lame + 22] =
            static_cast<std::uint8_t>(((o.delay & 0x0Fu) << 4) | ((o.padding >> 8) & 0x0Fu));
        f[lame + 23] = static_cast<std::uint8_t>(o.padding & 0xFFu);

        if (o.zero_crc) {
            f[lame + 34] = 0;
            f[lame + 35] = 0;
        } else {
            // The CRC covers every frame byte preceding the CRC field, which is
            // 190 for LAME's own full-flag layout and less when the TOC is
            // omitted. Compute it the general way, matching the parser.
            const std::uint16_t c = o.valid_crc ? crc16(f.data(), lame + 34) : 0xDEAD;
            f[lame + 34] = static_cast<std::uint8_t>((c >> 8) & 0xFFu);
            f[lame + 35] = static_cast<std::uint8_t>(c & 0xFFu);
        }
    }
    return f;
}

std::vector<std::uint8_t> make_opus_head(std::uint16_t pre_skip,
                                         std::int16_t gain_q78 = 0,
                                         std::uint8_t channels = 2,
                                         std::uint8_t version = 1,
                                         std::uint8_t mapping = 0) {
    std::vector<std::uint8_t> h(19, 0);
    std::memcpy(h.data(), "OpusHead", 8);
    h[8] = version;
    h[9] = channels;
    h[10] = static_cast<std::uint8_t>(pre_skip & 0xFFu);
    h[11] = static_cast<std::uint8_t>((pre_skip >> 8) & 0xFFu);
    h[12] = 0x80;
    h[13] = 0xBB;
    h[14] = 0x00;
    h[15] = 0x00;  // 48000 LE
    const auto g = static_cast<std::uint16_t>(gain_q78);
    h[16] = static_cast<std::uint8_t>(g & 0xFFu);
    h[17] = static_cast<std::uint8_t>((g >> 8) & 0xFFu);
    h[18] = mapping;
    return h;
}

}  // namespace

// ===========================================================================
//  MPEG frame header
// ===========================================================================

TEST(MpegHeader, ParsesMpeg1Layer3Stereo) {
    const auto f = make_mp3_frame({});
    auto h = parse_mpeg_frame_header(f);
    ASSERT_TRUE(h.has_value()) << h.error().to_log_string();
    EXPECT_EQ(h->version, MpegVersion::Mpeg1);
    EXPECT_EQ(h->layer, 3);
    EXPECT_EQ(h->bitrate_kbps, 128u);
    EXPECT_EQ(h->sample_rate_hz, 44100u);
    EXPECT_EQ(h->channels, 2);
    EXPECT_FALSE(h->is_mono);
    EXPECT_EQ(h->samples_per_frame, 1152u);
    EXPECT_EQ(h->side_info_bytes, 32u);
    EXPECT_EQ(h->frame_bytes, 417u);  // 1152/8 * 128000 / 44100 = 417
}

TEST(MpegHeader, MonoHasSmallerSideInfo) {
    Mp3Options o;
    o.mono = true;
    const auto f = make_mp3_frame(o);
    auto h = parse_mpeg_frame_header(f);
    ASSERT_TRUE(h.has_value());
    EXPECT_TRUE(h->is_mono);
    EXPECT_EQ(h->channels, 1);
    EXPECT_EQ(h->side_info_bytes, 17u);
}

TEST(MpegHeader, RejectsShortInput) {
    const std::uint8_t buf[3] = {0xFF, 0xFB, 0x90};
    auto h = parse_mpeg_frame_header(buf);
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code(), ErrorCode::UnexpectedEnd);
    EXPECT_FALSE(parse_mpeg_frame_header({}).has_value());
}

TEST(MpegHeader, RejectsBadSyncWord) {
    std::uint8_t buf[4] = {0x00, 0x00, 0x90, 0x00};
    auto h = parse_mpeg_frame_header(buf);
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code(), ErrorCode::MalformedHeader);

    buf[0] = 0xFF;
    buf[1] = 0x0B;  // sync bits incomplete
    EXPECT_FALSE(parse_mpeg_frame_header(buf).has_value());
}

TEST(MpegHeader, RejectsReservedVersionLayerAndRates) {
    // Reserved MPEG version (bits 4-3 = 01).
    std::uint8_t v[4] = {0xFF, 0xEB, 0x90, 0x00};
    EXPECT_FALSE(parse_mpeg_frame_header(v).has_value());

    // Reserved layer (bits 2-1 = 00).
    std::uint8_t l[4] = {0xFF, 0xF9, 0x90, 0x00};
    EXPECT_FALSE(parse_mpeg_frame_header(l).has_value());

    // Free bitrate index 0.
    std::uint8_t b[4] = {0xFF, 0xFB, 0x00, 0x00};
    EXPECT_FALSE(parse_mpeg_frame_header(b).has_value());

    // Reserved bitrate index 15.
    std::uint8_t b15[4] = {0xFF, 0xFB, 0xF0, 0x00};
    EXPECT_FALSE(parse_mpeg_frame_header(b15).has_value());

    // Reserved sample-rate index 3.
    std::uint8_t r[4] = {0xFF, 0xFB, 0x9C, 0x00};
    EXPECT_FALSE(parse_mpeg_frame_header(r).has_value());
}

TEST(MpegHeader, Mpeg2HasHalfTheSamplesPerFrame) {
    // MPEG-2 (bits 4-3 = 10), Layer III, 64 kbps, 22.05 kHz.
    std::uint8_t buf[4] = {0xFF, 0xF3, 0x80, 0x00};
    auto h = parse_mpeg_frame_header(buf);
    ASSERT_TRUE(h.has_value()) << h.error().to_log_string();
    EXPECT_EQ(h->version, MpegVersion::Mpeg2);
    EXPECT_EQ(h->samples_per_frame, 576u);
    EXPECT_EQ(h->sample_rate_hz, 22050u);
    EXPECT_EQ(h->side_info_bytes, 17u);
}

TEST(MpegHeader, Mpeg25IsRecognised) {
    std::uint8_t buf[4] = {0xFF, 0xE3, 0x80, 0x00};  // version bits 00
    auto h = parse_mpeg_frame_header(buf);
    ASSERT_TRUE(h.has_value()) << h.error().to_log_string();
    EXPECT_EQ(h->version, MpegVersion::Mpeg25);
    EXPECT_EQ(h->sample_rate_hz, 11025u);
    EXPECT_EQ(h->samples_per_frame, 576u);
}

// ===========================================================================
//  Xing / LAME  — REQ-AUD-037
// ===========================================================================

TEST(XingLame, ParsesDelayAndPaddingExactly) {
    Mp3Options o;
    o.delay = 576;
    o.padding = 1800;
    const auto f = make_mp3_frame(o);
    auto tag = parse_xing_lame(f);
    ASSERT_TRUE(tag.has_value()) << tag.error().to_log_string();
    EXPECT_TRUE(tag->has_lame);
    EXPECT_TRUE(tag->lame_crc_ok);
    EXPECT_EQ(tag->encoder_delay, 576u);
    EXPECT_EQ(tag->encoder_padding, 1800u);
    EXPECT_EQ(tag->frame_count, 10000u);
    EXPECT_STREQ(tag->encoder, "LAME3.100");
}

TEST(XingLame, HandlesFullTwelveBitFieldRange) {
    // 12 bits each: 0..4095. Boundary values must survive the packing.
    for (const std::uint32_t d : {0u, 1u, 529u, 576u, 1152u, 4094u, 4095u}) {
        for (const std::uint32_t p : {0u, 1u, 529u, 530u, 4095u}) {
            Mp3Options o;
            o.delay = d;
            o.padding = p;
            const auto f = make_mp3_frame(o);
            auto tag = parse_xing_lame(f);
            ASSERT_TRUE(tag.has_value()) << "d=" << d << " p=" << p;
            EXPECT_EQ(tag->encoder_delay, d) << "d=" << d << " p=" << p;
            EXPECT_EQ(tag->encoder_padding, p) << "d=" << d << " p=" << p;
        }
    }
}

TEST(XingLame, AppliesSpecFormulaExactly) {
    // REQ-AUD-037:  skip_start = delay + 529 ; skip_end = max(0, padding - 529)
    Mp3Options o;
    o.delay = 576;
    o.padding = 1800;
    const auto info = mp3_gapless_info(make_mp3_frame(o));
    EXPECT_EQ(info.source, GaplessSource::XingLame);
    EXPECT_EQ(info.skip_start_frames, 576u + 529u);  // 1105
    EXPECT_EQ(info.skip_end_frames, 1800u - 529u);   // 1271
    EXPECT_EQ(info.valid_frames, 10000ull * 1152ull);
}

TEST(XingLame, ClampsSkipEndAtZeroWhenPaddingIsSmall) {
    // padding < 529 must yield 0, never a wrapped huge unsigned value. This is
    // exactly the bug the max(0, ...) in the spec formula exists to prevent.
    for (const std::uint32_t p : {0u, 1u, 100u, 528u, 529u}) {
        Mp3Options o;
        o.delay = 576;
        o.padding = p;
        const auto info = mp3_gapless_info(make_mp3_frame(o));
        EXPECT_EQ(info.skip_end_frames, 0u) << "padding=" << p;
    }
    Mp3Options o;
    o.delay = 576;
    o.padding = 530;
    EXPECT_EQ(mp3_gapless_info(make_mp3_frame(o)).skip_end_frames, 1u);
}

TEST(XingLame, InfoMagicIsAcceptedLikeXing) {
    Mp3Options o;
    o.use_info_magic = true;
    auto tag = parse_xing_lame(make_mp3_frame(o));
    ASSERT_TRUE(tag.has_value());
    EXPECT_TRUE(tag->is_info_magic);
    EXPECT_TRUE(tag->has_lame);
    EXPECT_EQ(tag->encoder_delay, 576u);
}

TEST(XingLame, TocIsSkippedCorrectly) {
    // With a TOC present the LAME extension moves 100 bytes later; if the
    // offset walk is wrong the delay/padding come out as garbage.
    Mp3Options o;
    o.include_toc = true;
    o.delay = 1000;
    o.padding = 2000;
    auto tag = parse_xing_lame(make_mp3_frame(o));
    ASSERT_TRUE(tag.has_value());
    EXPECT_TRUE(tag->has_toc);
    EXPECT_EQ(tag->encoder_delay, 1000u);
    EXPECT_EQ(tag->encoder_padding, 2000u);
}

TEST(XingLame, MonoLayoutIsHandled) {
    Mp3Options o;
    o.mono = true;
    o.delay = 800;
    o.padding = 900;
    auto tag = parse_xing_lame(make_mp3_frame(o));
    ASSERT_TRUE(tag.has_value());
    EXPECT_EQ(tag->encoder_delay, 800u);
    EXPECT_EQ(tag->encoder_padding, 900u);
}

TEST(XingLame, MissingLameFallsBackToDecoderDelayOnly) {
    // REQ-AUD-038: no LAME tag -> skip_start = 529, skip_end = 0, source None.
    Mp3Options o;
    o.include_lame = false;
    const auto info = mp3_gapless_info(make_mp3_frame(o));
    EXPECT_EQ(info.source, GaplessSource::None);
    EXPECT_EQ(info.skip_start_frames, kMp3DecoderDelay);
    EXPECT_EQ(info.skip_end_frames, 0u);
    EXPECT_FALSE(info.supports_sample_exact_splice());
}

TEST(XingLame, CrcInvalidTagIsIgnored) {
    // REQ-AUD-039: a tag failing CRC must be ignored, not trusted.
    Mp3Options o;
    o.valid_crc = false;
    o.delay = 576;
    o.padding = 1800;
    const auto f = make_mp3_frame(o);

    auto tag = parse_xing_lame(f);
    ASSERT_TRUE(tag.has_value());
    EXPECT_TRUE(tag->has_lame);
    EXPECT_FALSE(tag->lame_crc_ok);

    const auto info = mp3_gapless_info(f);
    EXPECT_EQ(info.source, GaplessSource::None) << "a bad CRC must not be trusted";
    EXPECT_EQ(info.skip_start_frames, kMp3DecoderDelay);
    EXPECT_EQ(info.skip_end_frames, 0u);
}

TEST(XingLame, ZeroCrcMeansNotComputedAndIsAccepted) {
    // Many encoders leave the CRC field zero; refusing those would break
    // gapless on a large slice of real libraries.
    Mp3Options o;
    o.zero_crc = true;
    o.delay = 576;
    o.padding = 1800;
    const auto info = mp3_gapless_info(make_mp3_frame(o));
    EXPECT_EQ(info.source, GaplessSource::XingLame);
    EXPECT_EQ(info.skip_start_frames, 1105u);
}

TEST(XingLame, RejectsImplausibleTrimLargerThanStream) {
    // A 1-frame stream cannot have thousands of samples trimmed.
    Mp3Options o;
    o.frame_count = 1;
    o.delay = 4095;
    o.padding = 4095;
    const auto info = mp3_gapless_info(make_mp3_frame(o));
    EXPECT_EQ(info.source, GaplessSource::None);
    EXPECT_EQ(info.skip_start_frames, kMp3DecoderDelay);
}

TEST(XingLame, NoXingMagicIsReportedNotCrashed) {
    std::vector<std::uint8_t> f(1024, 0);
    f[0] = 0xFF;
    f[1] = 0xFB;
    f[2] = 0x90;
    f[3] = 0x00;
    auto tag = parse_xing_lame(f);
    EXPECT_FALSE(tag.has_value());
    // And the convenience wrapper still yields a usable fallback.
    const auto info = mp3_gapless_info(f);
    EXPECT_EQ(info.source, GaplessSource::None);
    EXPECT_EQ(info.skip_start_frames, kMp3DecoderDelay);
}

TEST(XingLame, TruncatedFrameIsRejectedAtEveryLength) {
    // Truncation must never yield a crash, a hang, or an out-of-range trim.
    // Note that a frame is not "too short" merely because it is cut off: for
    // this stereo no-TOC layout the LAME extension ends at byte 84, so lengths
    // at or beyond that legitimately carry a complete tag.
    const auto full = make_mp3_frame({});
    for (std::size_t n = 0; n < 120; ++n) {
        std::span<const std::uint8_t> part{full.data(), n};

        auto tag = parse_xing_lame(part);
        if (tag.has_value() && tag->has_lame) {
            // Both fields are 12 bits wide, so nothing may exceed 4095.
            EXPECT_LE(tag->encoder_delay, 4095u) << "n=" << n;
            EXPECT_LE(tag->encoder_padding, 4095u) << "n=" << n;
        }

        const auto info = mp3_gapless_info(part);
        EXPECT_LE(info.skip_start_frames, 4095u + kMp3DecoderDelay) << "n=" << n;
        EXPECT_LE(info.skip_end_frames, 4095u) << "n=" << n;
        EXPECT_TRUE(info.source == GaplessSource::None ||
                    info.source == GaplessSource::XingLame)
            << "n=" << n;
    }
}

// ===========================================================================
//  iTunSMPB — REQ-AUD-040, REQ-AUD-042
// ===========================================================================

TEST(ITunSmpb, ParsesCanonicalValue) {
    // field2 = priming (0x840 = 2112), field3 = padding (0x1C0 = 448),
    // field4 = total samples (0xA0F800 = 10549248).
    const std::string v =
        " 00000000 00000840 000001C0 0000000000A0F800 00000000 00000000 "
        "00000000 00000000 00000000 00000000 00000000 00000000";
    auto info = parse_itunsmpb(v);
    ASSERT_TRUE(info.has_value()) << info.error().to_log_string();
    EXPECT_EQ(info->source, GaplessSource::ITunSMPB);
    EXPECT_EQ(info->skip_start_frames, 0x840u);
    EXPECT_EQ(info->skip_end_frames, 0x1C0u);
    EXPECT_EQ(info->valid_frames, 0xA0F800ull);
    EXPECT_EQ(info->playable_frames(), 0xA0F800ull - 0x840ull - 0x1C0ull);
}

TEST(ITunSmpb, ToleratesExtraWhitespaceAndTabs) {
    auto info = parse_itunsmpb("\t 0 840 1C0 A0F800 \t 0 ");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->skip_start_frames, 0x840u);
}

TEST(ITunSmpb, RejectsTooFewFields) {
    EXPECT_FALSE(parse_itunsmpb("").has_value());
    EXPECT_FALSE(parse_itunsmpb("   ").has_value());
    EXPECT_FALSE(parse_itunsmpb("0").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 840").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 840 1C0").has_value());
    auto ok4 = parse_itunsmpb("0 840 1C0 A0F800");
    EXPECT_TRUE(ok4.has_value());
}

TEST(ITunSmpb, RejectsTooManyFields) {
    std::string many;
    for (int i = 0; i < 40; ++i) many += "00000000 ";
    EXPECT_FALSE(parse_itunsmpb(many).has_value());
}

TEST(ITunSmpb, RejectsNonHexadecimal) {
    EXPECT_FALSE(parse_itunsmpb("0 zzzz 1C0 A0F800").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 84g 1C0 A0F800").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 -840 1C0 A0F800").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 8.40 1C0 A0F800").has_value());
    EXPECT_FALSE(parse_itunsmpb("0 0x840 1C0 A0F800").has_value());
}

TEST(ITunSmpb, RejectsTrimExceedingSampleCount) {
    // REQ-AUD-042: priming + padding > total is inconsistent and must be
    // rejected outright rather than clamped into a plausible-looking value.
    auto r = parse_itunsmpb("0 FFFFFF FFFFFF 100");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::OutOfRange);
}

TEST(ITunSmpb, RejectsValuesExceeding32Bits) {
    auto r = parse_itunsmpb("0 FFFFFFFFFF 0 FFFFFFFFFFFF");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::OutOfRange);
}

TEST(ITunSmpb, CrossChecksAgainstStreamLength) {
    // A tag claiming 10x more samples than the stream holds is not trustworthy.
    auto r = parse_itunsmpb("0 840 1C0 A0F800", /*total_frames_hint=*/1000);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::OutOfRange);

    // A close match is accepted.
    auto good = parse_itunsmpb("0 840 1C0 A0F800", 0xA0F800);
    EXPECT_TRUE(good.has_value());
}

TEST(ITunSmpb, RejectsOverlongInput) {
    auto r = parse_itunsmpb(std::string(2000, 'A'));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::InputTooLarge);
}

TEST(ITunSmpb, AacFallbackIsNotAuthoritative) {
    // REQ-AUD-041: without the tag we use the decoder priming but must record
    // that the value is a guess, so the UI can say so.
    const auto info = aac_fallback_gapless_info();
    EXPECT_EQ(info.skip_start_frames, kAacDefaultPriming);
    EXPECT_EQ(info.source, GaplessSource::None);
    EXPECT_FALSE(info.supports_sample_exact_splice());
}

// ===========================================================================
//  OpusHead — REQ-AUD-043
// ===========================================================================

TEST(OpusHeadParse, ParsesPreSkipAndGain) {
    auto h = parse_opus_head(make_opus_head(312, 0));
    ASSERT_TRUE(h.has_value()) << h.error().to_log_string();
    EXPECT_EQ(h->pre_skip, 312u);
    EXPECT_EQ(h->channel_count, 2u);
    EXPECT_EQ(h->input_sample_rate_hz, 48000u);
    EXPECT_DOUBLE_EQ(h->output_gain_db(), 0.0);
}

TEST(OpusHeadParse, OutputGainIsQ7Point8Signed) {
    // +256 in Q7.8 is +1.0 dB; -256 is -1.0 dB. Sign handling is the risk here.
    EXPECT_DOUBLE_EQ(parse_opus_head(make_opus_head(312, 256))->output_gain_db(), 1.0);
    EXPECT_DOUBLE_EQ(parse_opus_head(make_opus_head(312, -256))->output_gain_db(), -1.0);
    EXPECT_DOUBLE_EQ(parse_opus_head(make_opus_head(312, -1536))->output_gain_db(), -6.0);
    EXPECT_NEAR(parse_opus_head(make_opus_head(312, 128))->output_gain_db(), 0.5, 1e-12);
}

TEST(OpusHeadParse, RejectsWrongMagic) {
    auto bad = make_opus_head(312);
    bad[0] = 'X';
    auto h = parse_opus_head(bad);
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code(), ErrorCode::MalformedHeader);
}

TEST(OpusHeadParse, RejectsTruncationAtEveryLength) {
    const auto full = make_opus_head(312);
    for (std::size_t n = 0; n < full.size(); ++n) {
        std::span<const std::uint8_t> part{full.data(), n};
        EXPECT_FALSE(parse_opus_head(part).has_value()) << "n=" << n;
    }
    EXPECT_TRUE(parse_opus_head(full).has_value());
}

TEST(OpusHeadParse, RejectsUnsupportedMajorVersion) {
    EXPECT_TRUE(parse_opus_head(make_opus_head(312, 0, 2, 0x01)).has_value());
    EXPECT_TRUE(parse_opus_head(make_opus_head(312, 0, 2, 0x0F)).has_value());
    EXPECT_FALSE(parse_opus_head(make_opus_head(312, 0, 2, 0x10)).has_value());
    EXPECT_FALSE(parse_opus_head(make_opus_head(312, 0, 2, 0xFF)).has_value());
}

TEST(OpusHeadParse, RejectsZeroChannels) {
    auto h = parse_opus_head(make_opus_head(312, 0, 0));
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code(), ErrorCode::MalformedHeader);
}

TEST(OpusHeadParse, RequiresMappingTableWhenPromised) {
    // Family 1 promises a channel mapping table; a 19-byte header lacks it.
    auto h = parse_opus_head(make_opus_head(312, 0, 2, 1, /*mapping=*/1));
    ASSERT_FALSE(h.has_value());
    EXPECT_EQ(h.error().code(), ErrorCode::UnexpectedEnd);

    auto with_table = make_opus_head(312, 0, 2, 1, 1);
    with_table.resize(23, 0);
    EXPECT_TRUE(parse_opus_head(with_table).has_value());
}

TEST(OpusGapless, UsesPreSkipAsHeadTrim) {
    const auto h = parse_opus_head(make_opus_head(312));
    ASSERT_TRUE(h.has_value());
    const auto info = gapless_from_opus_head(h.value(), 480000);
    EXPECT_EQ(info.source, GaplessSource::OpusHead);
    EXPECT_EQ(info.skip_start_frames, 312u);
    EXPECT_EQ(info.skip_end_frames, 0u);
    EXPECT_EQ(info.valid_frames, 480000u);
}

TEST(OpusGapless, RescalesPreSkipForNon48kOutput) {
    // pre_skip is defined in 48 kHz units; at 24 kHz output it halves.
    const auto h = parse_opus_head(make_opus_head(960));
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(gapless_from_opus_head(h.value(), 0, 24000).skip_start_frames, 480u);
    EXPECT_EQ(gapless_from_opus_head(h.value(), 0, 48000).skip_start_frames, 960u);
    EXPECT_EQ(gapless_from_opus_head(h.value(), 0, 96000).skip_start_frames, 1920u);
}

// ===========================================================================
//  Native and granule
// ===========================================================================

TEST(NativeGapless, TrimsNothing) {
    const auto info = native_gapless_info(1234567);
    EXPECT_EQ(info.source, GaplessSource::Native);
    EXPECT_EQ(info.skip_start_frames, 0u);
    EXPECT_EQ(info.skip_end_frames, 0u);
    EXPECT_EQ(info.valid_frames, 1234567u);
    EXPECT_EQ(info.playable_frames(), 1234567u);
    EXPECT_TRUE(info.supports_sample_exact_splice());
}

TEST(GranuleGapless, UsesFinalGranuleAsLength) {
    auto info = gapless_from_granule(480000);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->source, GaplessSource::Granule);
    EXPECT_EQ(info->valid_frames, 480000u);
    EXPECT_EQ(info->skip_start_frames, 0u);
}

TEST(GranuleGapless, HonoursNegativeInitialGranuleAsHeadTrim) {
    // REQ-AUD-045: a negative initial granule is an encoder-trimmed start.
    auto info = gapless_from_granule(480000, -1024);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->skip_start_frames, 1024u);
    EXPECT_EQ(info->valid_frames, 480000u);
}

TEST(GranuleGapless, RejectsNegativeFinalGranule) {
    auto info = gapless_from_granule(-1);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code(), ErrorCode::MalformedHeader);
}

TEST(GranuleGapless, RejectsHeadTrimExceedingLength) {
    auto info = gapless_from_granule(100, -1000);
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code(), ErrorCode::OutOfRange);
}

// A granule position is read out of a file, so it can be INT64_MIN — the one
// value whose negation is undefined behaviour, because the negative range is one
// wider than the positive one. Found by fuzz_gapless: UBSan reported "negation of
// -9223372036854775808 cannot be represented in type 'long int'" on the first
// replay of the committed corpus. The answer must be a plain rejection, not a
// trap, and not whatever the optimiser decides UB may become.
TEST(GranuleGapless, RejectsMostNegativeInitialGranuleWithoutOverflowing) {
    auto info = gapless_from_granule(1, std::numeric_limits<std::int64_t>::min());
    ASSERT_FALSE(info.has_value());
    EXPECT_EQ(info.error().code(), ErrorCode::OutOfRange);

    // One less extreme, still far past the 32-bit skip field: same answer, which
    // is what makes the case above a boundary rather than a special case.
    auto big = gapless_from_granule(1, -(std::int64_t{1} << 40));
    ASSERT_FALSE(big.has_value());
    EXPECT_EQ(big.error().code(), ErrorCode::OutOfRange);

    // And the largest trim that still fits, to prove the bound moved nowhere.
    auto ok = gapless_from_granule(std::int64_t{0xFFFFFFFF}, -std::int64_t{0xFFFFFFFF});
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->skip_start_frames, 0xFFFFFFFFu);
    EXPECT_EQ(ok->playable_frames(), 0u);
}

// ===========================================================================
//  Splice eligibility — REQ-AUD-046, REQ-AUD-047
// ===========================================================================

TEST(Splice, RequiresRealMetadataOnBothSides) {
    const auto good = native_gapless_info(480000);
    GaplessInfo none;
    none.source = GaplessSource::None;
    none.skip_start_frames = kMp3DecoderDelay;

    EXPECT_TRUE(can_splice_sample_exactly(good, good));
    EXPECT_FALSE(can_splice_sample_exactly(good, none));
    EXPECT_FALSE(can_splice_sample_exactly(none, good));
    EXPECT_FALSE(can_splice_sample_exactly(none, none));
}

TEST(Splice, RequiresKnownLengthOnTheOutgoingSide) {
    // Without knowing where the outgoing track ends there is nothing to splice
    // against, even if its metadata source is authoritative.
    GaplessInfo unknown_len = native_gapless_info(kUnknownFrames);
    const auto good = native_gapless_info(480000);
    EXPECT_FALSE(can_splice_sample_exactly(unknown_len, good));
    EXPECT_TRUE(can_splice_sample_exactly(good, unknown_len));
}

TEST(PlayableFrames, HandlesUnknownAndOvertrim) {
    GaplessInfo i;
    i.valid_frames = kUnknownFrames;
    EXPECT_EQ(i.playable_frames(), kUnknownFrames);

    i.valid_frames = 1000;
    i.skip_start_frames = 600;
    i.skip_end_frames = 600;
    EXPECT_EQ(i.playable_frames(), 0u) << "must clamp, never wrap";
}

TEST(SourceNames, AreStable) {
    EXPECT_EQ(to_string(GaplessSource::None), "none");
    EXPECT_EQ(to_string(GaplessSource::Native), "native");
    EXPECT_EQ(to_string(GaplessSource::XingLame), "xing-lame");
    EXPECT_EQ(to_string(GaplessSource::ITunSMPB), "itunsmpb");
    EXPECT_EQ(to_string(GaplessSource::OpusHead), "opushead");
    EXPECT_EQ(to_string(GaplessSource::Granule), "granule");
}

// ===========================================================================
//  Robustness against hostile input — §21.6 precursor to the fuzz targets
// ===========================================================================

TEST(Robustness, RandomBuffersNeverCrashOrHang) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed) — determinism
    std::mt19937 rng{0xC0FFEEu};  // fixed seed: reproducible
    std::uniform_int_distribution<int> byte{0, 255};

    for (int iter = 0; iter < 3000; ++iter) {
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(iter % 300));
        for (auto& b : buf) b = static_cast<std::uint8_t>(byte(rng));

        (void)parse_mpeg_frame_header(buf);
        (void)parse_xing_lame(buf);
        (void)parse_opus_head(buf);
        const auto info = mp3_gapless_info(buf);
        // The fallback contract must always hold.
        EXPECT_TRUE(info.source == GaplessSource::None ||
                    info.source == GaplessSource::XingLame);
    }
}

TEST(Robustness, MutatedValidFramesNeverProduceAbsurdTrim) {
    // Flip one byte at a time in a valid frame and assert the result is always
    // either rejected or sane. A wrong-but-plausible trim is the dangerous case.
    const auto base = make_mp3_frame({});
    for (std::size_t i = 0; i < 200 && i < base.size(); ++i) {
        auto f = base;
        f[i] = static_cast<std::uint8_t>(f[i] ^ 0xFFu);
        const auto info = mp3_gapless_info(f);
        if (info.source == GaplessSource::XingLame) {
            // 12-bit delay field maxes at 4095, plus the 529 decoder delay.
            EXPECT_LE(info.skip_start_frames, 4095u + kMp3DecoderDelay) << "byte " << i;
            EXPECT_LE(info.skip_end_frames, 4095u) << "byte " << i;
        }
    }
}

TEST(Robustness, RandomItunSmpbStringsAreSafe) {
    // NOLINTNEXTLINE(cert-msc32-c,cert-msc51-cpp,bugprone-random-generator-seed) — determinism
    std::mt19937 rng{0xBEEFu};
    std::uniform_int_distribution<int> ch{32, 126};
    for (int iter = 0; iter < 2000; ++iter) {
        std::string s;
        s.resize(static_cast<std::size_t>(iter % 80));
        for (auto& c : s) c = static_cast<char>(ch(rng));
        auto r = parse_itunsmpb(s);
        if (r.has_value()) {
            // Whatever it accepted must be internally consistent.
            if (r->valid_frames != kUnknownFrames) {
                EXPECT_LE(static_cast<std::uint64_t>(r->skip_start_frames) +
                              static_cast<std::uint64_t>(r->skip_end_frames),
                          r->valid_frames);
            }
        }
    }
}
