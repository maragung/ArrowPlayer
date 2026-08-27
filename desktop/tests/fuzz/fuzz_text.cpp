// SPDX-License-Identifier: MPL-2.0
/// Fuzz target — UTF-8 decoding, sanitisation and sort keys (§21.6).
///
/// Not one of REQ-SEC-011's seventeen named targets, and deliberately kept
/// separate from them: every one of those parsers reaches this code, because tag
/// values, file names, cue sheets and theme strings all arrive as bytes and become
/// `std::string`. A decoder that can be walked off the end of a buffer would be
/// exploitable through all seventeen at once, so it is fuzzed on its own account.
///
/// The invariants are the ones the header promises:
///
///   * `decode_utf8` always advances, so no input can make a caller loop forever.
///   * `sanitize_utf8` returns well-formed UTF-8 for every possible input.
///   * `utf8_offset_of` never lands inside a sequence, and never past the end.
///   * `sort_key` and the case conversions return well-formed UTF-8 given it.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/text.hpp"

namespace {

[[noreturn]] void fail(const char* what) {
    std::fprintf(stderr, "fuzz_text: invariant violated: %s\n", what);
    std::abort();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    // 1. Decoding always makes progress, and never reads past the end.
    std::size_t pos = 0;
    std::size_t codepoints = 0;
    while (pos < input.size()) {
        const std::size_t before = pos;
        (void)eclipse::text::decode_utf8(input, pos);
        if (pos <= before) {
            fail("decode_utf8 did not advance");
        }
        if (pos > input.size()) {
            fail("decode_utf8 advanced past the end of the input");
        }
        ++codepoints;
    }
    if (codepoints != eclipse::text::utf8_length(input)) {
        fail("utf8_length disagrees with a decode walk of the same input");
    }

    // 2. Sanitisation is total: valid UTF-8 out, for any bytes in.
    const std::string clean = eclipse::text::sanitize_utf8(input);
    if (!eclipse::text::is_valid_utf8(clean)) {
        fail("sanitize_utf8 returned invalid UTF-8");
    }

    // 3. Offsets land on boundaries. Checked against the sanitised string, where
    //    "boundary" is well defined; on malformed input every byte is its own
    //    replacement character and the property degenerates rather than failing.
    for (std::size_t n = 0; n <= codepoints + 1; ++n) {
        const std::size_t offset = eclipse::text::utf8_offset_of(clean, n);
        if (offset > clean.size()) {
            fail("utf8_offset_of returned an offset past the end");
        }
        if (offset < clean.size() &&
            (static_cast<unsigned char>(clean[offset]) & 0xC0u) == 0x80u) {
            fail("utf8_offset_of landed on a continuation byte");
        }
    }

    // 4. The transforms preserve well-formedness. sort_key() is the one that
    //    feeds the database index, so a malformed key would corrupt collation.
    if (!eclipse::text::is_valid_utf8(eclipse::text::to_lower(clean)) ||
        !eclipse::text::is_valid_utf8(eclipse::text::to_upper(clean)) ||
        !eclipse::text::is_valid_utf8(eclipse::text::to_title(clean)) ||
        !eclipse::text::is_valid_utf8(eclipse::text::collapse_whitespace(clean)) ||
        !eclipse::text::is_valid_utf8(eclipse::text::sort_key(clean, "en"))) {
        fail("a text transform returned invalid UTF-8 for valid UTF-8 input");
    }

    // 5. Substring extraction never splits a sequence, at any window.
    if (codepoints > 0) {
        const std::size_t start = codepoints / 3;
        const std::string part = eclipse::text::utf8_substr(clean, start, codepoints);
        if (!eclipse::text::is_valid_utf8(part)) {
            fail("utf8_substr split a UTF-8 sequence");
        }
    }

    // 6. Path safety, which is a security control rather than a convenience
    //    (REQ-THM-018, REQ-SEC-008) — it is what stands between a crafted skin
    //    archive and a zip-slip write. Two invariants, and the second is the one
    //    that matters: whatever normalisation accepts must itself be safe, or the
    //    caller has been handed an escape with a clean bill of health.
    std::string normalized;
    if (eclipse::text::normalize_relative_path(input, normalized)) {
        if (eclipse::text::is_unsafe_relative_path(normalized)) {
            fail("normalize_relative_path accepted a path it then calls unsafe");
        }
        if (normalized.find("..") != std::string::npos &&
            (normalized == ".." || normalized.starts_with("../") ||
             normalized.ends_with("/..") || normalized.find("/../") != std::string::npos)) {
            fail("normalize_relative_path returned a traversal segment");
        }
        if (!normalized.empty() && normalized.front() == '/') {
            fail("normalize_relative_path returned an absolute path");
        }
    }
    return 0;
}
