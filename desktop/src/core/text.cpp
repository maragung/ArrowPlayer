// SPDX-License-Identifier: MPL-2.0
#include "core/text.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace eclipse::text {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

// Continuation-byte check.
constexpr bool is_cont(unsigned char c) noexcept {
    return (c & 0xC0u) == 0x80u;
}

}  // namespace

// =========================================================================
//  UTF-8
// =========================================================================

char32_t decode_utf8(std::string_view s, std::size_t& pos) noexcept {
    if (pos >= s.size()) return 0;
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const unsigned char c0 = p[pos];

    // 1-byte / ASCII
    if (c0 < 0x80u) {
        ++pos;
        return c0;
    }

    // Lone continuation byte or invalid lead -> replacement, advance 1.
    if (c0 < 0xC2u) {
        ++pos;
        return kReplacement;
    }

    std::size_t need = 0;
    char32_t cp = 0;
    if (c0 < 0xE0u) {
        need = 1;
        cp = c0 & 0x1Fu;
    } else if (c0 < 0xF0u) {
        need = 2;
        cp = c0 & 0x0Fu;
    } else if (c0 < 0xF5u) {
        need = 3;
        cp = c0 & 0x07u;
    } else {
        ++pos;
        return kReplacement;
    }

    // We need bytes at pos+1 .. pos+need, so pos+need must be a valid index.
    if (pos + need >= s.size()) {
        ++pos;  // truncated sequence; advance one byte to guarantee progress
        return kReplacement;
    }
    for (std::size_t i = 1; i <= need; ++i) {
        const unsigned char ci = p[pos + i];
        if (!is_cont(ci)) {
            ++pos;
            return kReplacement;
        }
        cp = (cp << 6) | (ci & 0x3Fu);
    }

    // Reject surrogates and out-of-range.
    if ((cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
        ++pos;
        return kReplacement;
    }
    // Reject overlong encodings.
    if ((need == 1 && cp < 0x80u) || (need == 2 && cp < 0x800u) ||
        (need == 3 && cp < 0x10000u)) {
        ++pos;
        return kReplacement;
    }

    pos += need + 1;
    return cp;
}

bool is_valid_utf8(std::string_view s) noexcept {
    std::size_t pos = 0;
    while (pos < s.size()) {
        const std::size_t before = pos;
        if (decode_utf8(s, pos) == kReplacement) {
            // A genuine U+FFFD in the input is 3 bytes; a decode failure
            // advances by exactly 1. That distinguishes the two cases.
            if (pos - before != 3) return false;
        }
    }
    return true;
}

void encode_utf8(char32_t cp, std::string& out) {
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = kReplacement;
    if (cp < 0x80u) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::size_t utf8_length(std::string_view s) noexcept {
    std::size_t pos = 0, n = 0;
    while (pos < s.size()) {
        (void)decode_utf8(s, pos);
        ++n;
    }
    return n;
}

std::size_t utf8_offset_of(std::string_view s, std::size_t n) noexcept {
    std::size_t pos = 0, i = 0;
    while (pos < s.size() && i < n) {
        (void)decode_utf8(s, pos);
        ++i;
    }
    return pos;
}

std::string utf8_substr(std::string_view s, std::size_t start, std::size_t count) {
    const std::size_t b = utf8_offset_of(s, start);
    if (b >= s.size()) return {};
    if (count == std::string_view::npos) return std::string{s.substr(b)};
    std::size_t pos = b, i = 0;
    while (pos < s.size() && i < count) {
        (void)decode_utf8(s, pos);
        ++i;
    }
    return std::string{s.substr(b, pos - b)};
}

std::string sanitize_utf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (pos < s.size()) {
        const char32_t cp = decode_utf8(s, pos);
        encode_utf8(cp, out);
    }
    return out;
}

// =========================================================================
//  Case and diacritics
// =========================================================================

char32_t to_lower(char32_t cp) noexcept {
    if (cp < 0x80u) return (cp >= U'A' && cp <= U'Z') ? cp + 32 : cp;
    // Latin-1 Supplement: C0-DE -> E0-FE, excluding D7 (multiplication sign).
    if (cp >= 0x00C0u && cp <= 0x00DEu && cp != 0x00D7u) return cp + 0x20u;
    // Latin Extended-A: mostly even/odd pairs.
    if (cp >= 0x0100u && cp <= 0x017Fu) {
        // Exceptions where the pairing breaks.
        if (cp == 0x0130u || cp == 0x0131u || cp == 0x0178u) {
            if (cp == 0x0178u) return 0x00FFu;
            return cp;
        }
        if (cp >= 0x0139u && cp <= 0x0148u) return (cp % 2 == 1) ? cp + 1 : cp;
        if (cp >= 0x0179u && cp <= 0x017Eu) return (cp % 2 == 1) ? cp + 1 : cp;
        return (cp % 2 == 0) ? cp + 1 : cp;
    }
    // Greek.
    if (cp >= 0x0391u && cp <= 0x03A9u) return cp + 0x20u;
    // Cyrillic.
    if (cp >= 0x0410u && cp <= 0x042Fu) return cp + 0x20u;
    if (cp >= 0x0400u && cp <= 0x040Fu) return cp + 0x50u;
    return cp;
}

char32_t to_upper(char32_t cp) noexcept {
    if (cp < 0x80u) return (cp >= U'a' && cp <= U'z') ? cp - 32 : cp;
    if (cp >= 0x00E0u && cp <= 0x00FEu && cp != 0x00F7u) return cp - 0x20u;
    if (cp == 0x00FFu) return 0x0178u;
    if (cp >= 0x0100u && cp <= 0x017Fu) {
        if (cp >= 0x013Au && cp <= 0x0149u) return (cp % 2 == 0) ? cp - 1 : cp;
        if (cp >= 0x017Au && cp <= 0x017Eu) return (cp % 2 == 0) ? cp - 1 : cp;
        return (cp % 2 == 1) ? cp - 1 : cp;
    }
    if (cp >= 0x03B1u && cp <= 0x03C9u) return cp - 0x20u;
    if (cp >= 0x0430u && cp <= 0x044Fu) return cp - 0x20u;
    if (cp >= 0x0450u && cp <= 0x045Fu) return cp - 0x50u;
    return cp;
}

char32_t strip_diacritic(char32_t cp) noexcept {
    // Latin-1 Supplement.
    switch (cp) {
        case 0x00C0:
        case 0x00C1:
        case 0x00C2:
        case 0x00C3:
        case 0x00C4:
        case 0x00C5:
            return U'A';
        case 0x00C6:
            return U'A';  // AE -> A (single-char fold)
        case 0x00C7:
            return U'C';
        case 0x00C8:
        case 0x00C9:
        case 0x00CA:
        case 0x00CB:
            return U'E';
        case 0x00CC:
        case 0x00CD:
        case 0x00CE:
        case 0x00CF:
            return U'I';
        case 0x00D0:
            return U'D';
        case 0x00D1:
            return U'N';
        case 0x00D2:
        case 0x00D3:
        case 0x00D4:
        case 0x00D5:
        case 0x00D6:
        case 0x00D8:
            return U'O';
        case 0x00D9:
        case 0x00DA:
        case 0x00DB:
        case 0x00DC:
            return U'U';
        case 0x00DD:
            return U'Y';
        case 0x00DE:
            return U'T';  // thorn
        case 0x00DF:
            return U's';  // sharp s -> s
        case 0x00E0:
        case 0x00E1:
        case 0x00E2:
        case 0x00E3:
        case 0x00E4:
        case 0x00E5:
            return U'a';
        case 0x00E6:
            return U'a';
        case 0x00E7:
            return U'c';
        case 0x00E8:
        case 0x00E9:
        case 0x00EA:
        case 0x00EB:
            return U'e';
        case 0x00EC:
        case 0x00ED:
        case 0x00EE:
        case 0x00EF:
            return U'i';
        case 0x00F0:
            return U'd';
        case 0x00F1:
            return U'n';
        case 0x00F2:
        case 0x00F3:
        case 0x00F4:
        case 0x00F5:
        case 0x00F6:
        case 0x00F8:
            return U'o';
        case 0x00F9:
        case 0x00FA:
        case 0x00FB:
        case 0x00FC:
            return U'u';
        case 0x00FD:
        case 0x00FF:
            return U'y';
        case 0x00FE:
            return U't';
        default:
            break;
    }
    // Latin Extended-A, grouped by base letter.
    if (cp >= 0x0100u && cp <= 0x017Fu) {
        static constexpr std::array<char32_t, 128> kBase = {
            /*0100*/ U'A', U'a', U'A', U'a', U'A', U'a', U'C', U'c',
            /*0108*/ U'C', U'c', U'C', U'c', U'C', U'c', U'D', U'd',
            /*0110*/ U'D', U'd', U'E', U'e', U'E', U'e', U'E', U'e',
            /*0118*/ U'E', U'e', U'E', U'e', U'G', U'g', U'G', U'g',
            /*0120*/ U'G', U'g', U'G', U'g', U'H', U'h', U'H', U'h',
            /*0128*/ U'I', U'i', U'I', U'i', U'I', U'i', U'I', U'i',
            /*0130*/ U'I', U'i', U'I', U'i', U'J', U'j', U'K', U'k',
            /*0138*/ U'k', U'L', U'l', U'L', U'l', U'L', U'l', U'L',
            /*0140*/ U'l', U'L', U'l', U'N', U'n', U'N', U'n', U'N',
            /*0148*/ U'n', U'n', U'N', U'n', U'O', U'o', U'O', U'o',
            /*0150*/ U'O', U'o', U'O', U'o', U'R', U'r', U'R', U'r',
            /*0158*/ U'R', U'r', U'S', U's', U'S', U's', U'S', U's',
            /*0160*/ U'S', U's', U'T', U't', U'T', U't', U'T', U't',
            /*0168*/ U'U', U'u', U'U', U'u', U'U', U'u', U'U', U'u',
            /*0170*/ U'U', U'u', U'U', U'u', U'W', U'w', U'Y', U'y',
            /*0178*/ U'Y', U'Z', U'z', U'Z', U'z', U'Z', U'z', U's',
        };
        return kBase[cp - 0x0100u];
    }
    return cp;
}

std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (pos < s.size()) encode_utf8(to_lower(decode_utf8(s, pos)), out);
    return out;
}

std::string to_upper(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (pos < s.size()) encode_utf8(to_upper(decode_utf8(s, pos)), out);
    return out;
}

std::string to_title(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool at_word_start = true;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const char32_t cp = decode_utf8(s, pos);
        const bool boundary = is_space(cp) || cp == U'(' || cp == U'[' || cp == U'{' ||
                              cp == U'"' || cp == U'\'' || cp == U'-' || cp == U'/' ||
                              cp == U'.';
        encode_utf8(at_word_start ? to_upper(cp) : to_lower(cp), out);
        at_word_start = boundary;
    }
    return out;
}

// =========================================================================
//  Whitespace and classification
// =========================================================================

bool is_space(char32_t cp) noexcept {
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\v' ||
           cp == U'\f' || cp == 0x00A0u || cp == 0x2028u || cp == 0x2029u || cp == 0x3000u ||
           (cp >= 0x2000u && cp <= 0x200Au);
}

bool is_digit(char32_t cp) noexcept {
    return cp >= U'0' && cp <= U'9';
}

bool is_alpha(char32_t cp) noexcept {
    return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || cp >= 0x00C0u;
}

bool is_alnum(char32_t cp) noexcept {
    return is_alpha(cp) || is_digit(cp);
}

std::string_view trim_left(std::string_view s) noexcept {
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t next = pos;
        if (!is_space(decode_utf8(s, next))) break;
        pos = next;
    }
    return s.substr(pos);
}

std::string_view trim_right(std::string_view s) noexcept {
    while (!s.empty()) {
        // Walk back to the start of the last codepoint.
        std::size_t start = s.size() - 1;
        while (start > 0 && is_cont(static_cast<unsigned char>(s[start]))) --start;
        std::size_t probe = start;
        if (!is_space(decode_utf8(s, probe))) break;
        s = s.substr(0, start);
    }
    return s;
}

std::string_view trim(std::string_view s) noexcept {
    return trim_right(trim_left(s));
}

std::string collapse_whitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool in_space = false;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const char32_t cp = decode_utf8(s, pos);
        if (is_space(cp)) {
            in_space = true;
        } else {
            if (in_space && !out.empty()) out.push_back(' ');
            in_space = false;
            encode_utf8(cp, out);
        }
    }
    return out;
}

// =========================================================================
//  Comparison
// =========================================================================

namespace {
constexpr char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}
}  // namespace

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ascii_lower(a[i]) != ascii_lower(b[i])) return false;
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view s, std::string_view suffix) noexcept {
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

bool icontains(std::string_view s, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > s.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= s.size(); ++i)
        if (iequals(s.substr(i, needle.size()), needle)) return true;
    return false;
}

// =========================================================================
//  Sorting  (REQ-LIB-029)
// =========================================================================

std::vector<std::string> default_articles(std::string_view locale) {
    if (istarts_with(locale, "de")) return {"der", "die", "das", "den", "dem", "ein", "eine"};
    if (istarts_with(locale, "fr")) return {"le", "la", "les", "l'", "un", "une", "des"};
    if (istarts_with(locale, "es")) return {"el", "la", "los", "las", "un", "una"};
    if (istarts_with(locale, "it")) return {"il", "lo", "la", "i", "gli", "le", "un", "una"};
    if (istarts_with(locale, "nl")) return {"de", "het", "een"};
    if (istarts_with(locale, "id") || istarts_with(locale, "ms")) return {};  // no articles
    return {"the", "a", "an"};
}

std::string sort_key(std::string_view display, const std::vector<std::string>& articles) {
    // 1. casefold + 2. strip diacritics, in one pass.
    std::string folded;
    folded.reserve(display.size());
    std::size_t pos = 0;
    while (pos < display.size()) {
        const char32_t cp = decode_utf8(display, pos);
        encode_utf8(strip_diacritic(to_lower(cp)), folded);
    }

    // 3. collapse whitespace and trim.
    std::string key = collapse_whitespace(folded);

    // 4. strip a leading article, longest match first so "the" beats "t".
    std::size_t best = 0;
    for (const auto& art : articles) {
        if (art.empty() || art.size() <= best) continue;
        if (!istarts_with(key, art)) continue;
        // Must be followed by a separator, or be an elision like "l'".
        const bool elision = art.back() == '\'';
        if (elision) {
            best = art.size();
        } else if (key.size() > art.size() &&
                   (key[art.size()] == ' ' || key[art.size()] == '\'')) {
            best = art.size() + 1;  // consume the separator too
        }
    }
    if (best > 0 && best <= key.size()) key.erase(0, best);

    return std::string{trim(key)};
}

std::string sort_key(std::string_view display, std::string_view locale) {
    return sort_key(display, default_articles(locale));
}

// =========================================================================
//  Splitting and joining
// =========================================================================

std::vector<std::string_view> split(std::string_view s, char delim, bool keep_empty) noexcept {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = s.find(delim, start);
        std::string_view piece =
            (at == std::string_view::npos) ? s.substr(start) : s.substr(start, at - start);
        if (keep_empty || !piece.empty()) parts.push_back(piece);
        if (at == std::string_view::npos) break;
        start = at + 1;
    }
    return parts;
}

std::vector<std::string> split_multi(std::string_view s,
                                     const std::vector<std::string>& separators) {
    // Longest separator first, so " / " wins over "/".
    std::vector<std::string> seps = separators;
    std::sort(seps.begin(), seps.end(), [](const std::string& a, const std::string& b) {
        return a.size() > b.size();
    });

    std::vector<std::string> out;
    std::string current;
    std::size_t i = 0;
    while (i < s.size()) {
        bool matched = false;
        for (const auto& sep : seps) {
            if (sep.empty() || i + sep.size() > s.size()) continue;
            if (iequals(s.substr(i, sep.size()), sep)) {
                out.emplace_back(trim(current));
                current.clear();
                i += sep.size();
                matched = true;
                break;
            }
        }
        if (!matched) current.push_back(s[i++]);
    }
    out.emplace_back(trim(current));

    // Drop empties produced by leading/trailing/doubled separators.
    out.erase(
        std::remove_if(out.begin(), out.end(), [](const std::string& v) { return v.empty(); }),
        out.end());
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view glue) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(glue);
        out.append(parts[i]);
    }
    return out;
}

std::string replace_all(std::string_view s, std::string_view find, std::string_view repl) {
    if (find.empty()) return std::string{s};
    std::string out;
    out.reserve(s.size());
    std::size_t start = 0;
    while (true) {
        const std::size_t at = s.find(find, start);
        if (at == std::string_view::npos) {
            out.append(s.substr(start));
            break;
        }
        out.append(s.substr(start, at - start));
        out.append(repl);
        start = at + find.size();
    }
    return out;
}

// =========================================================================
//  Numbers
// =========================================================================

bool parse_int(std::string_view s, std::int64_t& out) noexcept {
    const std::string_view t = trim(s);
    if (t.empty()) return false;

    std::size_t i = 0;
    bool negative = false;
    if (t[0] == '+' || t[0] == '-') {
        negative = (t[0] == '-');
        i = 1;
    }
    if (i >= t.size()) return false;

    // The magnitude limit is asymmetric: int64 reaches 2^63 going down but only
    // 2^63 - 1 going up. Guarding both with the positive limit would reject
    // INT64_MIN, whose magnitude is exactly one past it.
    constexpr std::uint64_t kMaxPositive =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = negative ? kMaxPositive + 1u : kMaxPositive;

    std::uint64_t acc = 0;
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(t[i] - '0');
        if (acc > (limit - digit) / 10) return false;  // overflow
        acc = acc * 10 + digit;
    }

    if (!negative) {
        out = static_cast<std::int64_t>(acc);
        return true;
    }
    // Negating 2^63 as int64 is undefined, so INT64_MIN is named, not computed.
    out = (acc == kMaxPositive + 1u) ? std::numeric_limits<std::int64_t>::min()
                                     : -static_cast<std::int64_t>(acc);
    return true;
}

bool parse_double(std::string_view s, double& out) noexcept {
    const std::string_view t = trim(s);
    if (t.empty() || t.size() > 64) return false;
    char buf[65];
    std::memcpy(buf, t.data(), t.size());
    buf[t.size()] = '\0';
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(buf, &end);
    if (errno == ERANGE || end != buf + t.size()) return false;
    out = v;
    return true;
}

bool parse_hex(std::string_view s, std::uint64_t& out) noexcept {
    const std::string_view t = trim(s);
    if (t.empty() || t.size() > 16) return false;
    std::uint64_t acc = 0;
    for (const char c : t) {
        std::uint64_t d;
        if (c >= '0' && c <= '9')
            d = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = static_cast<std::uint64_t>(c - 'A' + 10);
        else
            return false;
        acc = (acc << 4) | d;
    }
    out = acc;
    return true;
}

// =========================================================================
//  Glob and edit distance
// =========================================================================

bool glob_match(std::string_view pattern,
                std::string_view subject,
                bool case_sensitive) noexcept {
    // Iterative backtracking: linear space, no recursion, so a hostile pattern
    // cannot blow the stack (contrast with regex, excluded by REQ-PLS-013).
    std::size_t p = 0, s = 0, star_p = std::string_view::npos, star_s = 0;

    const auto norm = [case_sensitive](char c) noexcept {
        return case_sensitive ? c : ascii_lower(c);
    };

    while (s < subject.size()) {
        if (p < pattern.size() && pattern[p] == '[') {
            // Character class.
            std::size_t close = p + 1;
            bool negate = false;
            if (close < pattern.size() && (pattern[close] == '!' || pattern[close] == '^')) {
                negate = true;
                ++close;
            }
            bool matched = false;
            std::size_t i = close;
            for (; i < pattern.size() && (pattern[i] != ']' || i == close); ++i) {
                if (i + 2 < pattern.size() && pattern[i + 1] == '-' && pattern[i + 2] != ']') {
                    if (norm(subject[s]) >= norm(pattern[i]) &&
                        norm(subject[s]) <= norm(pattern[i + 2]))
                        matched = true;
                    i += 2;
                } else if (norm(pattern[i]) == norm(subject[s])) {
                    matched = true;
                }
            }
            if (i < pattern.size() && matched != negate) {
                p = i + 1;
                ++s;
                continue;
            }
        } else if (p < pattern.size() &&
                   (pattern[p] == '?' || norm(pattern[p]) == norm(subject[s]))) {
            ++p;
            ++s;
            continue;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star_p = p++;
            star_s = s;
            continue;
        }

        if (star_p != std::string_view::npos) {
            p = star_p + 1;
            s = ++star_s;
            continue;
        }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::size_t edit_distance(std::string_view a,
                          std::string_view b,
                          std::size_t max_distance) noexcept {
    const std::size_t n = a.size(), m = b.size();
    if (n > m + max_distance || m > n + max_distance) return max_distance + 1;
    if (n == 0) return m;
    if (m == 0) return n;

    // Two rolling rows plus one extra for the transposition term.
    std::vector<std::size_t> prev2(m + 1), prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = j;

    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        std::size_t row_min = cur[0];
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t cost = (a[i - 1] == b[j - 1]) ? 0u : 1u;
            std::size_t v = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                v = std::min(v, prev2[j - 2] + 1);  // transposition
            cur[j] = v;
            row_min = std::min(row_min, v);
        }
        if (row_min > max_distance) return max_distance + 1;
        prev2 = prev;
        prev = cur;
    }
    return prev[m] > max_distance ? max_distance + 1 : prev[m];
}

// =========================================================================
//  Paths  (REQ-THM-018 / REQ-SEC-008)
// =========================================================================

bool is_unsafe_relative_path(std::string_view p) noexcept {
    if (p.empty()) return true;
    if (p.size() > 200) return true;  // REQ-THM-017

    // Absolute, UNC, or drive-letter prefixed.
    if (p.front() == '/' || p.front() == '\\') return true;
    if (p.size() >= 2 && p[1] == ':') return true;
    if (p.size() >= 2 && p[0] == '\\' && p[1] == '\\') return true;

    // NUL or control characters.
    for (const char c : p) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc == 0 || uc < 0x20 || uc == 0x7F) return true;
    }

    // Traversal segments, on either separator.
    std::size_t start = 0;
    for (std::size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            const std::string_view seg = p.substr(start, i - start);
            if (seg == "..") return true;
            start = i + 1;
        }
    }
    return false;
}

bool normalize_relative_path(std::string_view p, std::string& out) {
    out.clear();
    if (p.empty()) return false;

    // REQ-THM-018 states one conjoined requirement: an entry path must be
    // "relative, normalised, free of `..` segments, free of absolute prefixes and
    // drive letters, free of NUL and control characters". Normalisation is
    // therefore not a weaker, separate step — a path this function returns true
    // for has to be one the security check would also accept. Rejecting the
    // forbidden classes here means a caller that only ever calls this function
    // cannot be handed an absolute or NUL-bearing path with a clean result.
    //
    // Found by fuzz_text (REQ-SEC-011): "../etc/passwd" was already refused, but
    // "/absolute/path" was quietly relativised and a name containing a NUL was
    // accepted verbatim — on POSIX that name truncates at the NUL, which is a
    // path-confusion vector, not a cosmetic defect.
    if (p.front() == '/' || p.front() == '\\') return false;
    if (p.size() >= 2 && p[1] == ':') return false;
    for (const char c : p) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc == 0 || uc < 0x20 || uc == 0x7F) return false;
    }

    std::vector<std::string_view> stack;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            const std::string_view seg = p.substr(start, i - start);
            start = i + 1;
            if (seg.empty() || seg == ".") continue;
            if (seg == "..") {
                if (stack.empty()) return false;  // would escape the root
                stack.pop_back();
                continue;
            }
            stack.push_back(seg);
        }
    }
    if (stack.empty()) return false;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (i) out.push_back('/');
        out.append(stack[i]);
    }
    // The postcondition asserted rather than assumed, so the two functions cannot
    // drift: whatever class is added to the security check in future — the
    // REQ-THM-017 length cap is one already — applies to this result too.
    if (is_unsafe_relative_path(out)) {
        out.clear();
        return false;
    }
    return true;
}

}  // namespace eclipse::text
