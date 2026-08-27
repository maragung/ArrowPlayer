// SPDX-License-Identifier: MPL-2.0
/// Fuzz target `fuzz_gapless` — supporting target, **not** one of `REQ-SEC-011`'s
/// seventeen. See `README.md` for the ledger.
///
/// `fuzz_xinglame` covers the MP3 side of `audio/decode/gapless_info.hpp`. Three
/// other parsers in the same header read equally untrusted bytes and had no
/// coverage at all:
///
///   * `parse_itunsmpb` — a free-form MP4 tag value. `REQ-AUD-042` names it a fuzz
///     target in so many words, and its failure mode is arithmetic rather than
///     memory: three hex fields become a skip, a padding and a sample count.
///   * `parse_opus_head` — RFC 7845 §5.1, where `channel_mapping` promises a table
///     whose length is derived from `channel_count`, i.e. one field sizing a read
///     of the same buffer.
///   * `gapless_from_granule` — two signed granule positions, where `-initial`
///     becomes an unsigned head skip. Signed-to-unsigned on attacker input.
///
/// The eventual `fuzz_mp4atoms` (Phase 2) will drive `parse_itunsmpb` through a
/// real atom tree. That target is about the tree; this one is about the value, and
/// the value parser exists today.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

#include "audio/decode/gapless_info.hpp"

namespace {

using namespace eclipse::audio;

[[noreturn]] void fail(const char* what) {
    (void)std::fprintf(stderr, "fuzz_gapless: invariant violated: %s\n", what);
    std::abort();
}

/// Every accepted GaplessInfo, whatever produced it, has to satisfy these.
void check_common(const GaplessInfo& info, GaplessSource expected) {
    if (info.source != expected) {
        fail("a parser attributed its answer to another source");
    }
    if (info.valid_frames == kUnknownFrames) {
        if (info.playable_frames() != kUnknownFrames) {
            fail("an unknown stream length produced a known playable count");
        }
    } else {
        if (info.playable_frames() > info.valid_frames) {
            fail("more playable frames than the stream contains");
        }
        const std::uint64_t trim = static_cast<std::uint64_t>(info.skip_start_frames) +
                                   static_cast<std::uint64_t>(info.skip_end_frames);
        if (info.playable_frames() !=
            (trim >= info.valid_frames ? 0u : info.valid_frames - trim)) {
            fail("playable_frames does not equal length minus trim");
        }
    }
    // REQ-AUD-046: a splice is only sample-exact when both sides are
    // authoritative and the outgoing side knows where its audio ends.
    if (can_splice_sample_exactly(info, info) && info.valid_frames == kUnknownFrames) {
        fail("a sample-exact splice was offered without knowing where audio ends");
    }
}

/// Reads a big-endian 64-bit value out of the input, or 0 past the end. Used to
/// derive the numeric arguments the non-byte-oriented entry points take, so a
/// mutation anywhere in the buffer can still reach them.
std::uint64_t be64_at(std::span<const std::uint8_t> in, std::size_t offset) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const std::size_t at = offset + i;
        v = (v << 8) | (at < in.size() ? in[at] : 0u);
    }
    return v;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);

    // ------------------------------------------------------------------
    // 1. iTunSMPB — REQ-AUD-040, REQ-AUD-042.
    // ------------------------------------------------------------------
    const std::string_view value(reinterpret_cast<const char*>(data), size);

    // Twice: once with no idea how long the stream is, once with a hint, because
    // the cross-check against the real length is a separate rejection path and a
    // hint must never make an otherwise-good tag produce a *worse* answer.
    const std::uint64_t hint = be64_at(input, 0);

    for (const std::uint64_t total_hint : {kUnknownFrames, hint}) {
        auto tag = parse_itunsmpb(value, total_hint);
        if (!tag) {
            if (tag.error().user_message().empty()) {
                fail("a rejected iTunSMPB tag produced an error with no message");
            }
            continue;
        }
        const GaplessInfo& info = tag.value();
        check_common(info, GaplessSource::ITunSMPB);

        // REQ-AUD-042 in its own words: the trim cannot exceed the stream, and no
        // arithmetic here may wrap. An accepted tag that trims to nothing would
        // be silence at every boundary, which REQ-AUD-035 forbids.
        if (info.valid_frames != kUnknownFrames) {
            const std::uint64_t trim = static_cast<std::uint64_t>(info.skip_start_frames) +
                                       static_cast<std::uint64_t>(info.skip_end_frames);
            if (trim > info.valid_frames) {
                fail("an accepted iTunSMPB tag trims past the end of the stream");
            }
        }
        // valid_frames is either unknown or a real count; the parser maps the
        // tag's zero onto kUnknownFrames rather than a zero-length stream.
        if (info.valid_frames == 0) {
            fail("an accepted iTunSMPB tag reported a zero-sample stream");
        }
    }

    // ------------------------------------------------------------------
    // 2. OpusHead — REQ-AUD-043.
    // ------------------------------------------------------------------
    if (auto head = parse_opus_head(input)) {
        const OpusHead& h = head.value();
        if (h.channel_count == 0) {
            fail("an accepted OpusHead declares zero channels");
        }
        if ((h.version & 0xF0u) != 0x00u) {
            fail("an accepted OpusHead has an unsupported major version");
        }
        // A mapping family that promises a table must have had it; the parser
        // checks the length, so the bytes are there to read.
        if (h.channel_mapping == 1 || h.channel_mapping == 2) {
            const std::size_t needed = 21 + static_cast<std::size_t>(h.channel_count);
            if (size < needed) {
                fail("an accepted OpusHead promised a channel mapping table it lacks");
            }
        }
        // output_gain is Q7.8 over a full int16, so the decibel value is bounded
        // by ±128 dB. A NaN or an out-of-range gain would be applied to samples.
        const double gain_db = h.output_gain_db();
        if (!(gain_db >= -129.0 && gain_db <= 129.0)) {
            fail("OpusHead output gain is out of range or not a number");
        }

        // The rescale in gapless_from_opus_head multiplies pre_skip by the output
        // rate, so an absurd rate is the overflow candidate. Feed it one.
        const auto rate = static_cast<std::uint32_t>(be64_at(input, 8) & 0xFFFFFFFFu);
        for (const std::uint32_t output_rate : {48000u, 44100u, 8000u, 0u, rate}) {
            const GaplessInfo info = gapless_from_opus_head(h, be64_at(input, 16), output_rate);
            check_common(info, GaplessSource::OpusHead);
            // pre_skip is defined at 48 kHz; rescaling to a lower rate can only
            // reduce it, and to a higher rate can only increase it. Saturation is
            // permitted — silent wrapping is not.
            if (output_rate != 0 && output_rate < 48000 &&
                info.skip_start_frames > h.pre_skip) {
                fail("rescaling pre_skip to a lower rate increased it");
            }
            if ((output_rate == 0 || output_rate == 48000) &&
                info.skip_start_frames != h.pre_skip) {
                fail("pre_skip changed without a rate change to justify it");
            }
            if (info.skip_end_frames != 0) {
                fail("OpusHead carries no tail padding, but one was reported");
            }
        }
    } else {
        if (head.error().user_message().empty()) {
            fail("a rejected OpusHead produced an error with no message");
        }
    }

    // ------------------------------------------------------------------
    // 3. Ogg granule — REQ-AUD-045.
    // ------------------------------------------------------------------
    const auto final_granule = static_cast<std::int64_t>(be64_at(input, 0));
    const auto initial_granule = static_cast<std::int64_t>(be64_at(input, 8));

    if (auto gran = gapless_from_granule(final_granule, initial_granule)) {
        const GaplessInfo& info = gran.value();
        check_common(info, GaplessSource::Granule);
        if (final_granule < 0) {
            fail("a negative final granule was accepted as a stream length");
        }
        if (info.valid_frames != static_cast<std::uint64_t>(final_granule)) {
            fail("the granule length was not carried through unchanged");
        }
        // A negative initial granule is a head trim; a non-negative one is not.
        if (initial_granule >= 0 && info.skip_start_frames != 0) {
            fail("a head trim appeared without a negative initial granule");
        }
        if (info.skip_start_frames > info.valid_frames) {
            fail("the head trim exceeds the stream it trims");
        }
    } else {
        if (gran.error().user_message().empty()) {
            fail("a rejected granule pair produced an error with no message");
        }
    }

    // ------------------------------------------------------------------
    // 4. The two infallible constructors, on the same derived numbers.
    // ------------------------------------------------------------------
    check_common(native_gapless_info(be64_at(input, 16)), GaplessSource::Native);

    const GaplessInfo aac = aac_fallback_gapless_info(
        static_cast<std::uint32_t>(be64_at(input, 8) & 0xFFFFFFFFu), be64_at(input, 16));
    // REQ-AUD-041: the fallback is a guess, so it must not claim authority — and
    // a guess must never be spliced sample-exactly.
    check_common(aac, GaplessSource::None);
    if (can_splice_sample_exactly(aac, aac)) {
        fail("the REQ-AUD-041 fallback offered a sample-exact splice");
    }
    return 0;
}
