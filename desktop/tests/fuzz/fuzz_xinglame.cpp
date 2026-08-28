// SPDX-License-Identifier: MPL-2.0
/// Fuzz target `fuzz_xinglame` — REQ-SEC-011, one of the seventeen named targets.
///
/// The MP3 gapless path reads attacker-controlled bytes out of the first frame of
/// a file and turns them into sample counts. Two things can go wrong, and this
/// target watches for both:
///
///   * memory safety — the tag's TOC, LAME block and CRC are all at offsets
///     derived from *other* fields in the same buffer, which is the classic shape
///     of an out-of-bounds read;
///   * a wrong answer — `REQ-AUD-035` demands sample-identical concatenation, so a
///     trim larger than the stream is not a rounding error, it is silence or a
///     click at every track boundary.
///
/// `mp3_gapless_info` is documented as infallible (`REQ-AUD-038`: an unparseable
/// tag must never stop playback), which makes it the interesting call here: it
/// cannot report a problem, so every guarantee it offers has to hold structurally.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>

#include "audio/decode/gapless_info.hpp"

namespace {

using namespace arrow::audio;

[[noreturn]] void fail(const char* what) {
    (void)std::fprintf(stderr, "fuzz_xinglame: invariant violated: %s\n", what);
    std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);

    // 1. The frame header. Every field it reports is used to compute an offset
    //    into the same buffer, so a nonsense value here becomes a bad read later.
    if (auto header = parse_mpeg_frame_header(input)) {
        const MpegFrameHeader& h = header.value();
        if (h.sample_rate_hz == 0) {
            fail("an accepted frame header reports a zero sample rate");
        }
        if (h.bitrate_kbps == 0) {
            fail("an accepted frame header reports the free bitrate index");
        }
        if (h.channels != 1 && h.channels != 2) {
            fail("an accepted frame header reports an impossible channel count");
        }
        if (h.is_mono != (h.channels == 1)) {
            fail("is_mono disagrees with the channel count");
        }
        if (h.layer < 1 || h.layer > 3) {
            fail("an accepted frame header reports a reserved layer");
        }
        if (h.samples_per_frame != 384 && h.samples_per_frame != 576 &&
            h.samples_per_frame != 1152) {
            fail("an accepted frame header reports a non-MPEG frame size");
        }
        if (h.frame_bytes < 4 || h.side_info_bytes + 4u > h.frame_bytes) {
            fail("side information does not fit inside the frame it belongs to");
        }
    } else {
        if (header.error().user_message().empty()) {
            fail("a rejected frame header produced an error with no message");
        }
    }

    // 2. The Xing/Info tag itself.
    if (auto tag = parse_xing_lame(input)) {
        const XingLameTag& t = tag.value();
        // Both are 12-bit fields; a wider value means they were unpacked wrongly.
        if (t.encoder_delay > 0xFFFu || t.encoder_padding > 0xFFFu) {
            fail("an unpacked LAME delay/padding field exceeds 12 bits");
        }
        if (t.lame_crc_ok && !t.has_lame) {
            fail("a CRC was validated for a LAME block that is not present");
        }
        // The encoder string is copied out of the buffer; it must stay terminated
        // inside its own array or every consumer of it reads past the end.
        bool terminated = false;
        for (const char c : t.encoder) {
            if (c == '\0') {
                terminated = true;
                break;
            }
        }
        if (!terminated) {
            fail("the encoder string is not NUL-terminated within its array");
        }
    } else {
        if (tag.error().user_message().empty()) {
            fail("a rejected Xing/Info tag produced an error with no message");
        }
    }

    // 3. The convenience wrapper, which cannot fail and therefore must be right.
    const GaplessInfo info = mp3_gapless_info(input);

    if (info.source != GaplessSource::None && info.source != GaplessSource::XingLame) {
        fail("the MP3 path attributed its answer to a non-MP3 source");
    }
    if (info.source == GaplessSource::None &&
        (info.skip_start_frames != kMp3DecoderDelay || info.skip_end_frames != 0)) {
        fail("the REQ-AUD-038 fallback did not use the conservative defaults");
    }
    if (info.skip_start_frames < kMp3DecoderDelay) {
        fail("a skip smaller than the REQ-AUD-037 decoder delay of 529");
    }
    if (info.valid_frames != kUnknownFrames) {
        const std::uint64_t trim =
            static_cast<std::uint64_t>(info.skip_start_frames) + info.skip_end_frames;
        if (info.source == GaplessSource::XingLame && trim >= info.valid_frames) {
            // gapless_from_xing_lame is required to demote such a tag to None
            // rather than hand the scheduler a stream that trims to nothing.
            fail("a trusted tag trims away the whole stream");
        }
        if (info.playable_frames() > info.valid_frames) {
            fail("more playable frames than the stream contains");
        }
    } else if (info.playable_frames() != kUnknownFrames) {
        fail("an unknown length produced a known playable count");
    }

    // 4. Splicing is only ever offered when the metadata is authoritative — the
    //    predicate the gapless scheduler will branch on (REQ-AUD-046).
    if (can_splice_sample_exactly(info, info)) {
        if (info.source == GaplessSource::None) {
            fail("a sample-exact splice was offered for guessed metadata");
        }
        if (info.valid_frames == kUnknownFrames) {
            fail("a sample-exact splice was offered without knowing where audio ends");
        }
    }
    return 0;
}
