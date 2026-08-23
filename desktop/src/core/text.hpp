// SPDX-License-Identifier: MPL-2.0
// Text utilities — spec §9.2.3 (sort keys), §9.2.4 (Unicode correctness).
//
// Everything here is pure and dependency-free (REQ-GEN-050): no ICU, no Qt.
// The subset of Unicode behaviour we need is narrow and well defined:
//   * UTF-8 validation and codepoint iteration
//   * ASCII + Latin-1-supplement + Latin-Extended-A diacritic folding
//   * casefolding for the ranges that matter to library sorting
//   * leading-article stripping per locale
// Full NFKD is deliberately out of scope for the domain layer; where a real
// normalisation is required the adapter layer may supply one, but sort keys
// must remain computable with zero dependencies so they are testable and
// identical on both platforms (REQ-GEN-031).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eclipse::text {

// --------------------------------------------------------------------- UTF-8

/// Decodes one codepoint starting at `pos`. On success advances `pos` past the
/// sequence and returns the codepoint. On malformed input, advances by one byte
/// and returns U+FFFD, so callers always make progress and never loop forever
/// on hostile input.
[[nodiscard]] char32_t decode_utf8(std::string_view s, std::size_t& pos) noexcept;

/// True if `s` is well-formed UTF-8 (no overlongs, no surrogates, no truncation).
[[nodiscard]] bool is_valid_utf8(std::string_view s) noexcept;

/// Appends `cp` to `out` as UTF-8. Invalid codepoints are encoded as U+FFFD.
void encode_utf8(char32_t cp, std::string& out);

/// Number of codepoints (not bytes). Malformed sequences count as one each.
[[nodiscard]] std::size_t utf8_length(std::string_view s) noexcept;

/// Byte offset of codepoint index `n`, or s.size() if beyond the end.
[[nodiscard]] std::size_t utf8_offset_of(std::string_view s, std::size_t n) noexcept;

/// Substring by codepoint index/count, never splitting a sequence.
[[nodiscard]] std::string utf8_substr(std::string_view s,
                                      std::size_t start,
                                      std::size_t count = std::string_view::npos);

/// Replaces malformed sequences with U+FFFD, returning valid UTF-8.
[[nodiscard]] std::string sanitize_utf8(std::string_view s);

// ------------------------------------------------------------------ case/fold

[[nodiscard]] char32_t to_lower(char32_t cp) noexcept;
[[nodiscard]] char32_t to_upper(char32_t cp) noexcept;

[[nodiscard]] std::string to_lower(std::string_view s);
[[nodiscard]] std::string to_upper(std::string_view s);

/// Title case: first letter of each word upper, rest lower. Word boundaries are
/// whitespace and the characters ( [ { " ' - / .
[[nodiscard]] std::string to_title(std::string_view s);

/// Strips diacritics for Latin-1 Supplement and Latin Extended-A
/// (e.g. "Björk" -> "Bjork", "Mötley Crüe" -> "Motley Crue").
[[nodiscard]] char32_t strip_diacritic(char32_t cp) noexcept;

// ---------------------------------------------------------------- whitespace

[[nodiscard]] std::string_view trim(std::string_view s) noexcept;
[[nodiscard]] std::string_view trim_left(std::string_view s) noexcept;
[[nodiscard]] std::string_view trim_right(std::string_view s) noexcept;

/// Collapses runs of whitespace to a single U+0020.
[[nodiscard]] std::string collapse_whitespace(std::string_view s);

[[nodiscard]] bool is_space(char32_t cp) noexcept;
[[nodiscard]] bool is_digit(char32_t cp) noexcept;
[[nodiscard]] bool is_alpha(char32_t cp) noexcept;
[[nodiscard]] bool is_alnum(char32_t cp) noexcept;

// -------------------------------------------------------------------- compare

/// ASCII-case-insensitive equality. For identifiers and keywords only —
/// never for user-visible sorting, which must use sort_key().
[[nodiscard]] bool iequals(std::string_view a, std::string_view b) noexcept;
[[nodiscard]] bool istarts_with(std::string_view s, std::string_view prefix) noexcept;
[[nodiscard]] bool iends_with(std::string_view s, std::string_view suffix) noexcept;
[[nodiscard]] bool icontains(std::string_view s, std::string_view needle) noexcept;

// -------------------------------------------------------------------- sorting

/// Locale-specific leading articles stripped by sort_key(). "en" -> the/a/an.
/// REQ-LIB-029: configurable, defaulting to the UI language.
[[nodiscard]] std::vector<std::string> default_articles(std::string_view locale);

/// Computes the normalised sort key per REQ-LIB-029:
///   casefold -> strip diacritics -> strip leading article -> collapse spaces.
/// Sorting uses this; display always uses the original string.
[[nodiscard]] std::string sort_key(std::string_view display,
                                    const std::vector<std::string>& articles);

/// Convenience overload using `default_articles(locale)`.
[[nodiscard]] std::string sort_key(std::string_view display,
                                    std::string_view locale = "en");

// -------------------------------------------------------------------- splitting

/// Splits on `delim`, optionally trimming and dropping empties.
[[nodiscard]] std::vector<std::string_view> split(std::string_view s,
                                                  char delim,
                                                  bool keep_empty = true) noexcept;

/// Splits on any of the multi-character separators, longest match first.
/// REQ-LIB-028: used for multi-valued artist/genre fields. `,` is deliberately
/// NOT a default separator ("Earth, Wind & Fire").
[[nodiscard]] std::vector<std::string> split_multi(
    std::string_view s, const std::vector<std::string>& separators);

[[nodiscard]] std::string join(const std::vector<std::string>& parts,
                                std::string_view glue);

[[nodiscard]] std::string replace_all(std::string_view s,
                                       std::string_view find,
                                       std::string_view repl);

// -------------------------------------------------------------------- numbers

/// Strict integer parse: the whole (trimmed) input must be consumed.
/// Returns false on overflow, so hostile input cannot wrap.
[[nodiscard]] bool parse_int(std::string_view s, std::int64_t& out) noexcept;
[[nodiscard]] bool parse_double(std::string_view s, double& out) noexcept;

/// Hex parse for tag fields such as iTunSMPB (§8.4.2).
[[nodiscard]] bool parse_hex(std::string_view s, std::uint64_t& out) noexcept;

// -------------------------------------------------------------------- matching

/// Glob match supporting * ? and [chars] with [!negation]. Deliberately NOT
/// regex: REQ-PLS-013 excludes regex because catastrophic backtracking is a
/// denial-of-service surface in a shareable smart-playlist rule.
[[nodiscard]] bool glob_match(std::string_view pattern,
                               std::string_view subject,
                               bool case_sensitive = false) noexcept;

/// Damerau-Levenshtein distance, bounded by `max_distance`; returns
/// max_distance + 1 when it would exceed the bound (§9.5 fuzzy stage).
[[nodiscard]] std::size_t edit_distance(std::string_view a,
                                         std::string_view b,
                                         std::size_t max_distance) noexcept;

// -------------------------------------------------------------------- paths

/// True if `p` contains a traversal segment, an absolute prefix, a drive
/// letter, a NUL, or a control character. REQ-THM-018 / REQ-SEC-008: this is a
/// security control, not a convenience.
[[nodiscard]] bool is_unsafe_relative_path(std::string_view p) noexcept;

/// Normalises separators to '/' and resolves '.' and '..' textually.
/// Returns false if the result would escape the root.
[[nodiscard]] bool normalize_relative_path(std::string_view p, std::string& out);

}  // namespace eclipse::text
