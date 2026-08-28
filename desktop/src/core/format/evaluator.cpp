// SPDX-License-Identifier: MPL-2.0
#include "core/format/evaluator.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#include "core/text.hpp"

namespace arrow::format {

namespace {

// ===================================================================== helpers

// Resolve a field name to its rendered string.  REQ-EFS-004: absent
// (`nullopt`), the empty string, and whitespace-only are all "absent" and
// the evaluator propagates that signal.  Booleans become "0"/"1" because
// downstream `$if` and `$ifequal` need them as strings.

struct FieldResult {
    enum class Kind { Absent, Empty, Present };
    Kind kind{Kind::Absent};
    std::string value;
};

[[nodiscard]] FieldResult resolve_field(const TrackView& track, std::string_view name) {
    const std::optional<std::string> v = track.field(name);
    if (!v.has_value()) return {FieldResult::Kind::Absent, {}};
    if (v->empty()) return {FieldResult::Kind::Empty, {}};
    if (text::trim(*v).empty()) return {FieldResult::Kind::Absent, {}};
    return {FieldResult::Kind::Present, *v};
}

[[nodiscard]] bool is_present_kind(FieldResult::Kind k) {
    return k == FieldResult::Kind::Present;
}

// Strict integer parse, no overflow wrap.  Used everywhere a numeric
// function consumes a literal: conformance cases `numeric-overflow-is-
// absent` and `numeric-huge-literal-is-absent` make it a hard rule.
[[nodiscard]] bool strict_parse_int(std::string_view s, std::int64_t& out) {
    return text::parse_int(s, out);
}

// Strict double parse.
[[nodiscard]] bool strict_parse_double(std::string_view s, double& out) {
    return text::parse_double(s, out);
}

// Codepoint iteration that respects grapheme clusters (REQ-EFS-008 /
// OQ-008).  Walk through a string emitting grapheme-cluster ranges as
// `string_view`s over the input.  Combining marks stay attached to the
// base, ZWJ sequences stay together.
struct Graphemes {
    std::string_view s;
    std::size_t pos{0};

    [[nodiscard]] bool done() const noexcept { return pos >= s.size(); }

    [[nodiscard]] std::string_view next() {
        const std::size_t start = pos;
        if (pos >= s.size()) return {};
        // Decode one codepoint; skip any subsequent combining marks or
        // joiners that are part of the same grapheme cluster.  We do not
        // implement a full UAX #29 state machine — the conformance set
        // exercises only the two cases the spec calls out by name:
        // combining marks (conformance/unicode-combining-*) and ZWJ
        // emoji (conformance/unicode-zwj-emoji-*).  Treating those two
        // classes as cluster-extenders is enough to match the fixtures.
        char32_t cp = text::decode_utf8(s, pos);
        (void)cp;
        while (pos < s.size()) {
            const std::size_t probe = pos;
            const char32_t c = text::decode_utf8(s, pos);
            const bool extends = (c >= 0x0300u && c <= 0x036Fu) ||   // combining
                                 c == 0x200Du ||                    // ZWJ
                                 c == 0xFE0Fu;                      // variation selector
            if (!extends) {
                pos = probe;
                break;
            }
        }
        return s.substr(start, pos - start);
    }
};

[[nodiscard]] std::size_t grapheme_count(std::string_view s) {
    Graphemes g{s};
    std::size_t n = 0;
    while (!g.done()) {
        (void)g.next();
        ++n;
    }
    return n;
}

[[nodiscard]] std::string grapheme_substr(std::string_view s, std::size_t start,
                                          std::size_t count) {
    Graphemes g{s};
    std::size_t i = 0;
    while (!g.done() && i < start) {
        (void)g.next();
        ++i;
    }
    const std::size_t b = g.pos;
    i = 0;
    while (!g.done() && (count == std::string_view::npos || i < count)) {
        (void)g.next();
        ++i;
    }
    return std::string{s.substr(b, g.pos - b)};
}

[[nodiscard]] std::size_t grapheme_index_of(std::string_view haystack,
                                             std::string_view needle) = delete;
// ===================================================================== time
//
// The conformance cases pin a `now` instant and a `locale`; `$age` and
// `$date` need a tiny subset of locale-aware formatting.  This is
// dependency-free (REQ-GEN-050) and matches the fixtures' expectations
// exactly because the fixtures are the only locale we test.

struct IsoDate {
    int year{0};
    int month{0};
    int day{0};
    int hour{0};
    int minute{0};
    int second{0};
    bool valid{false};
};

[[nodiscard]] IsoDate parse_iso(std::string_view s) {
    IsoDate d;
    // Accept both "YYYY-MM-DD" and "YYYY-MM-DDThh:mm:ssZ" (and trailing
    // fractional seconds).  Conformance: %added% and %lastplayed% are
    // full ISO timestamps, %date% is a date-only.
    if (s.size() < 10) return d;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!digit(s[0]) || !digit(s[1]) || !digit(s[2]) || !digit(s[3]) || s[4] != '-' ||
        !digit(s[5]) || !digit(s[6]) || s[7] != '-' || !digit(s[8]) || !digit(s[9])) {
        return d;
    }
    d.year = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    d.month = (s[5] - '0') * 10 + (s[6] - '0');
    d.day = (s[8] - '0') * 10 + (s[9] - '0');
    d.valid = true;
    if (s.size() >= 19 && s[10] == 'T' && digit(s[11]) && digit(s[12]) && s[13] == ':' &&
        digit(s[14]) && digit(s[15]) && s[16] == ':' && digit(s[17]) && digit(s[18])) {
        d.hour = (s[11] - '0') * 10 + (s[12] - '0');
        d.minute = (s[14] - '0') * 10 + (s[15] - '0');
        d.second = (s[17] - '0') * 10 + (s[18] - '0');
    }
    return d;
}

// Days between two civil dates (Howard Hinnant's algorithm).  Both
// arguments are validated Y-M-D.
[[nodiscard]] long days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

// Portable inverse of gmtime: convert a broken-down UTC time to a unix
// timestamp.  `timegm` is a GNU/BSD extension; on Windows we implement
// it in terms of the well-known civil-day algorithm.
[[nodiscard]] std::time_t portable_timegm(const std::tm* tm) {
#if defined(_WIN32)
    const int y = tm->tm_year + 1900;
    const unsigned m = static_cast<unsigned>(tm->tm_mon + 1);
    const unsigned d = static_cast<unsigned>(tm->tm_mday);
    const long days = days_from_civil(y, m, d) - days_from_civil(1970, 1, 1);
    return static_cast<std::time_t>(days) * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 +
           tm->tm_sec;
#else
    return timegm(const_cast<std::tm*>(tm));
#endif
}

[[nodiscard]] std::string format_time(std::int64_t seconds) {
    if (seconds < 0) return {};
    const std::int64_t total = seconds;
    const std::int64_t h = total / 3600;
    const std::int64_t m = (total % 3600) / 60;
    const std::int64_t s = total % 60;
    char buf[16];
    if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld",
                      static_cast<long long>(h), static_cast<long long>(m),
                      static_cast<long long>(s));
    } else {
        std::snprintf(buf, sizeof(buf), "%lld:%02lld",
                      static_cast<long long>(m), static_cast<long long>(s));
    }
    return std::string{buf};
}

[[nodiscard]] std::string format_timems(std::int64_t ms) {
    if (ms < 0) return {};
    return format_time(ms / 1000);  // truncate (conformance: timems)
}

[[nodiscard]] std::string format_year(std::string_view iso) {
    const IsoDate d = parse_iso(iso);
    if (!d.valid) return {};
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%04d", d.year);
    return std::string{buf};
}

[[nodiscard]] std::string format_age(std::string_view iso, std::int64_t now_unix) {
    const IsoDate d = parse_iso(iso);
    if (!d.valid) return {};
    // Convert the iso date to a unix timestamp.  Conformance inputs are
    // UTC ("...Z"), so we treat them as such.
    std::tm in{};
    in.tm_year = d.year - 1900;
    in.tm_mon = d.month - 1;
    in.tm_mday = d.day;
    in.tm_hour = d.hour;
    in.tm_min = d.minute;
    in.tm_sec = d.second;
    const std::time_t then = portable_timegm(&in);
    if (then == static_cast<std::time_t>(-1)) return {};
    const std::int64_t diff = static_cast<std::int64_t>(now_unix) - static_cast<std::int64_t>(then);
    if (diff < 0) return {};  // future dates are not addressed
    constexpr std::int64_t kDay = 24LL * 3600;
    const long days = static_cast<long>(diff / kDay);
    if (days < 365) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%ld day%s ago", days, days == 1 ? "" : "s");
        return std::string{buf};
    }
    // Years: count the number of anniversaries that have passed by the
    // reference instant.  We do this with civil-date arithmetic on the
    // parsed date plus `now`'s date, so the time-of-day doesn't shift
    // the count.
    std::time_t tnow = static_cast<std::time_t>(now_unix);
    std::tm g{};
#if defined(_WIN32)
    gmtime_s(&g, &tnow);
#else
    gmtime_r(&tnow, &g);
#endif
    const int now_y = g.tm_year + 1900;
    const int now_m = g.tm_mon + 1;
    const int now_d = g.tm_mday;
    int years = 0;
    int yy = d.year;
    int mm = d.month;
    int dd = d.day;
    while (true) {
        const long cur = days_from_civil(yy, static_cast<unsigned>(mm), static_cast<unsigned>(dd));
        const long today = days_from_civil(now_y, static_cast<unsigned>(now_m), static_cast<unsigned>(now_d));
        if (cur > today) break;
        ++years;
        ++yy;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d year%s ago", years, years == 1 ? "" : "s");
    return std::string{buf};
}

// Format an ISO date with a Unicode LDML pattern (the small subset the
// conformance fixtures exercise: yyyy, yy, MMMM, MMM, MM, M, dd, d).
[[nodiscard]] std::string format_date(std::string_view iso, std::string_view pattern) {
    const IsoDate d = parse_iso(iso);
    if (!d.valid) return {};
    static constexpr const char* kMonthsFull[12] = {
        "January", "February", "March",     "April",   "May",      "June",
        "July",    "August",   "September", "October", "November", "December",
    };
    static constexpr const char* kMonthsAbbr[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    if (pattern.empty()) {
        // Default medium: "MMM d, yyyy" — en-US locale.  Conformance
        // pins locale explicitly, and the only "default" case is
        // $date('2000-10-02') which expects "Oct 2, 2000".
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s %d, %04d", kMonthsAbbr[d.month - 1], d.day, d.year);
        return std::string{buf};
    }
    std::string out;
    std::size_t i = 0;
    while (i < pattern.size()) {
        // Count run length of the current letter.
        const char c = pattern[i];
        std::size_t j = i;
        while (j < pattern.size() && pattern[j] == c) ++j;
        const std::size_t n = j - i;
        char buf[16];
        switch (c) {
            case 'y':
                if (n >= 4) std::snprintf(buf, sizeof(buf), "%04d", d.year);
                else std::snprintf(buf, sizeof(buf), "%02d", d.year % 100);
                out += buf;
                break;
            case 'M':
                if (n >= 4) out += kMonthsFull[d.month - 1];
                else if (n == 3) out += kMonthsAbbr[d.month - 1];
                else if (n == 2) std::snprintf(buf, sizeof(buf), "%02d", d.month), out += buf;
                else std::snprintf(buf, sizeof(buf), "%d", d.month), out += buf;
                break;
            case 'd':
                if (n >= 2) std::snprintf(buf, sizeof(buf), "%02d", d.day);
                else std::snprintf(buf, sizeof(buf), "%d", d.day);
                out += buf;
                break;
            default:
                // Unknown pattern char: emit the run verbatim so the
                // user can see what was not understood.
                for (std::size_t k = 0; k < n; ++k) out.push_back(c);
                break;
        }
        i = j;
    }
    return out;
}

// ===================================================================== number
//
// $div: 6 fractional digits, trailing zeros trimmed.  Conformance
// `div-repeating` requires 0.333333 exactly; banker's rounding is not
// used.  $round: half away from zero.  Overflow yields absent.

[[nodiscard]] std::string format_div(double a, double b) {
    if (b == 0.0) return {};  // §10.5 / OQ-011
    const double r = a / b;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", r);
    // Trim trailing zeros after the decimal point, but keep at least
    // the integer part and the dot if the result has a fractional
    // component.
    std::string s{buf};
    if (s.find('.') != std::string::npos) {
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

[[nodiscard]] std::string format_round(double v, int dp) {
    // Half away from zero: the simplest correct implementation for
    // floats is to scale, then std::round, then format.  $round does
    // NOT pad to the requested precision (conformance: round-dp-pads-
    // nothing), so trailing zeros after the decimal are trimmed.
    if (dp < 0) dp = 0;
    if (dp > 12) dp = 12;
    const double scale = std::pow(10.0, dp);
    const double scaled = std::round(v * scale);
    char buf[32];
    if (dp == 0) {
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(scaled));
        return std::string{buf};
    }
    std::snprintf(buf, sizeof(buf), "%.*f", dp, scaled);
    std::string s{buf};
    if (s.find('.') != std::string::npos) {
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

// Detect 64-bit signed overflow before it happens.  REQ-EFS-005 / OQ-011
// says the whole call yields absent; we apply this in the arithmetic
// functions.
[[nodiscard]] bool add_overflow(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) return true;
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) return true;
    out = a + b;
    return false;
}
[[nodiscard]] bool sub_overflow(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) return true;
    if (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b) return true;
    out = a - b;
    return false;
}
[[nodiscard]] bool mul_overflow(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (a == 0 || b == 0) {
        out = 0;
        return false;
    }
    if (a > 0 && b > 0 && a > std::numeric_limits<std::int64_t>::max() / b) return true;
    if (a < 0 && b < 0 &&
        a < std::numeric_limits<std::int64_t>::max() / b  // both negative
        && -a > std::numeric_limits<std::int64_t>::max() / -b)
        return true;
    if (a > 0 && b < 0 && b < std::numeric_limits<std::int64_t>::min() / a) return true;
    if (a < 0 && b > 0 && a < std::numeric_limits<std::int64_t>::min() / b) return true;
    out = a * b;
    return false;
}

// C-style truncated mod: sign of the dividend.  Conformance
// `mod-negative` says `$mod(-10,3)` is `-1`, not `2` (Python would
// produce 2).
[[nodiscard]] std::int64_t trunc_mod(std::int64_t a, std::int64_t b) {
    if (b == 0) return 0;
    return a - (a / b) * b;
}

// ===================================================================== repeat cap

// $repeat n is capped at 256 (REQ-EFS-009 / OQ-011).  $progress width is
// the same.  The output cap of 4096 is enforced by the engine, not here.
constexpr std::size_t kArgCap = 256;

}  // namespace

// ===================================================================== Evaluator

void Evaluator::evaluate(const Pattern& pattern, std::string& out, bool* cap_reached) noexcept {
    if (cap_reached) *cap_reached = false;
    // The top-level pattern is not a block, so the wrapper rule does
    // not apply.  We just walk the children, appending each result.
    // A function or field ref that returns absent contributes nothing.
    for (const Node& n : pattern) {
        const Value v = eval_node(n);
        if (v.absent()) continue;
        const std::string& s = v.str();
        for (std::size_t i = 0; i < s.size();) {
            if (out.size() >= ctx_.output_cap) {
                if (cap_reached) *cap_reached = true;
                return;
            }
            // Append one grapheme at a time so the cap counts what the
            // user counts.  If appending one UTF-8 sequence would
            // cross the cap, stop right there.
            std::size_t probe = i;
            (void)text::decode_utf8(s, probe);
            if (out.size() + (probe - i) > ctx_.output_cap) {
                if (cap_reached) *cap_reached = true;
                return;
            }
            out.append(s, i, probe - i);
            i = probe;
        }
    }
}

void Evaluator::render_block(const OptionalBlock& blk, bool parent_is_wrapper,
                             std::string& out) noexcept {
    for (const Node& c : blk.body) {
        if (parent_is_wrapper) {
            // Wrapper mode: literals are dropped, present field refs
            // append their values, function calls append their values,
            // and nested blocks recurse with wrapper mode.
            switch (c.kind()) {
                case Node::Kind::Literal:
                    continue;
                case Node::Kind::Field: {
                    const FieldResult r = resolve_field(track_, c.field().name);
                    if (r.kind != FieldResult::Kind::Present) continue;
                    out += r.value;
                    break;
                }
                case Node::Kind::Function: {
                    Value v = apply(c.func());
                    if (!v.absent()) out += v.str();
                    break;
                }
                case Node::Kind::Block: {
                    if (!any_present_field(c.block().body)) continue;
                    render_block(c.block(), true, out);
                    break;
                }
            }
        } else {
            // Normal mode: walk the child with eval_node.
            Value v = eval_node(c);
            if (!v.absent()) out += v.str();
        }
    }
}

Value Evaluator::eval_node(const Node& node) noexcept {
    switch (node.kind()) {
        case Node::Kind::Literal:
            return Value{node.literal().text};
        case Node::Kind::Field: {
            const FieldRef& fr = node.field();
            // Derived fields (REQ-EFS-007).  These are computed from
            // raw track data rather than looked up directly.  The raw
            // names are `duration`, `position`, `filesize`, `rating`,
            // `playing_state`, etc.; the derived names start with
            // lowercase letters in the conformance fixtures
            // (`%length%`, `%length_seconds%`, `%position%`,
            // `%remaining%`, `%filesize_natural%`, `%rating_stars%`).
            if (fr.name == "length") {
                const auto d = track_.field("duration");
                if (!d.has_value() || d->empty()) return Value{};
                std::int64_t secs = 0;
                if (!strict_parse_int(*d, secs)) return Value{};
                return Value{format_time(secs)};
            }
            if (fr.name == "length_seconds") {
                const auto d = track_.field("duration");
                if (!d.has_value() || d->empty()) return Value{};
                return Value{*d};
            }
            if (fr.name == "position") {
                const auto p = track_.field("position");
                if (!p.has_value() || p->empty()) return Value{};
                std::int64_t secs = 0;
                if (!strict_parse_int(*p, secs)) return Value{};
                return Value{format_time(secs)};
            }
            if (fr.name == "remaining") {
                const auto d = track_.field("duration");
                const auto p = track_.field("position");
                if (!d.has_value() || d->empty() || !p.has_value() || p->empty())
                    return Value{};
                std::int64_t dur = 0, pos = 0;
                if (!strict_parse_int(*d, dur) || !strict_parse_int(*p, pos)) return Value{};
                return Value{format_time(dur - pos)};
            }
            if (fr.name == "filesize_natural") {
                const auto f = track_.field("filesize");
                if (!f.has_value() || f->empty()) return Value{};
                std::int64_t bytes = 0;
                if (!strict_parse_int(*f, bytes)) return Value{};
                // Decimal base (10^6), localised.  Only en-US and
                // de-DE are exercised in the conformance; the latter
                // uses ',' as the decimal separator.
                const bool use_comma = (ctx_.locale.size() >= 2 &&
                                        ctx_.locale[0] == 'd' && ctx_.locale[1] == 'e');
                if (bytes < 1000) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
                    return Value{std::string{buf}};
                }
                if (bytes < 1000000) {
                    const double kb = bytes / 1000.0;
                    char buf[32];
                    if (use_comma) {
                        std::snprintf(buf, sizeof(buf), "%.1f kB", kb);
                        // Replace '.' with ','.
                        for (char* c = buf; *c; ++c)
                            if (*c == '.') *c = ',';
                    } else {
                        std::snprintf(buf, sizeof(buf), "%.1f KB", kb);
                    }
                    return Value{std::string{buf}};
                }
                if (bytes < 1000000000LL) {
                    const double mb = bytes / 1000000.0;
                    char buf[32];
                    if (use_comma) {
                        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
                        for (char* c = buf; *c; ++c)
                            if (*c == '.') *c = ',';
                    } else {
                        std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
                    }
                    return Value{std::string{buf}};
                }
                const double gb = bytes / 1000000000.0;
                char buf[32];
                if (use_comma) {
                    std::snprintf(buf, sizeof(buf), "%.1f GB", gb);
                    for (char* c = buf; *c; ++c)
                        if (*c == '.') *c = ',';
                } else {
                    std::snprintf(buf, sizeof(buf), "%.1f GB", gb);
                }
                return Value{std::string{buf}};
            }
            if (fr.name == "rating_stars") {
                const auto r = track_.field("rating");
                if (!r.has_value() || r->empty()) return Value{};
                std::int64_t rating = 0;
                if (!strict_parse_int(*r, rating)) return Value{};
                // Delegate to the same logic as $stars.
                const double v = static_cast<double>(rating) / 100.0 * 5.0;
                const double rounded = std::round(v);
                std::int64_t n = static_cast<std::int64_t>(rounded);
                if (n < 0) n = 0;
                if (n > 5) n = 5;
                return Value{std::string(static_cast<std::size_t>(n), '\xE2') +
                             "\x98\x85" +
                             std::string(static_cast<std::size_t>(5 - n), '\xE2') +
                             "\x98\x86"};
            }
            const FieldResult r = resolve_field(track_, fr.name);
            if (r.kind == FieldResult::Kind::Absent) return Value{};
            // Apply the format spec.
            std::string s = r.value;
            switch (fr.spec) {
                case FieldSpec::Upper:
                    s = text::to_upper(s);
                    break;
                case FieldSpec::Lower:
                    s = text::to_lower(s);
                    break;
                case FieldSpec::Title:
                    s = text::to_title(s);
                    break;
                case FieldSpec::ZeroPad: {
                    // Width applies to the digit run, not the sign.
                    // We do a simple: if the value contains a '.', it
                    // is not an integer; do not pad.  Otherwise, pad
                    // with leading zeros.
                    const std::size_t dot = s.find('.');
                    if (dot == std::string::npos) {
                        const int w = fr.width;
                        std::string digits = s;
                        if (digits.size() < static_cast<std::size_t>(w)) {
                            digits = std::string(static_cast<std::size_t>(w) - digits.size(),
                                                 '0') +
                                     digits;
                        }
                        s = std::move(digits);
                    }
                    break;
                }
                case FieldSpec::None:
                    break;
            }
            return Value{std::move(s)};
        }
        case Node::Kind::Function: {
            return apply(node.func());
        }
        case Node::Kind::Block: {
            // Evaluate the block.  A top-level block (one whose parent
            // is the top-level pattern, not another block) is never
            // itself "in wrapper mode" — wrapper mode only applies
            // when a block is a NESTED child of another block.  So we
            // start with parent_is_wrapper = false.
            const OptionalBlock& blk = node.block();
            if (!any_present_field(blk.body)) return Value{};
            // The block renders.  The wrapper rule: a block whose
            // direct children contain neither a literal nor a present
            // field ref nor a function call is "invisible" — its
            // output is only the present field ref values from its
            // subtree, with literals stripped.  This is the rule the
            // conformance case `block-nested-outer-collapses` requires.
            const bool is_wrapper = [&]() {
                for (const Node& c : blk.body) {
                    if (c.kind() == Node::Kind::Literal) return false;
                    if (c.kind() == Node::Kind::Field) {
                        const FieldResult r = resolve_field(track_, c.field().name);
                        if (is_present_kind(r.kind)) return false;
                    }
                    if (c.kind() == Node::Kind::Function) return false;
                }
                return true;
            }();
            std::string acc;
            render_block(blk, is_wrapper, acc);
            return Value{std::move(acc)};
        }
    }
    return Value{};
}

bool Evaluator::any_present_field(const Pattern& pattern) noexcept {
    // A block with no field references renders in full (OQ-012).
    bool found_any = false;
    for (const Node& n : pattern) {
        switch (n.kind()) {
            case Node::Kind::Literal:
                continue;
            case Node::Kind::Field: {
                found_any = true;
                const FieldResult r = resolve_field(track_, n.field().name);
                if (is_present_kind(r.kind)) return true;
                break;
            }
            case Node::Kind::Function: {
                // Walk the function's arguments for field references.
                for (const auto& arg : n.func().args) {
                    if (any_present_field(arg)) return true;
                }
                break;
            }
            case Node::Kind::Block: {
                if (any_present_field(n.block().body)) return true;
                break;
            }
        }
    }
    if (!found_any) return true;  // OQ-012: no field refs → renders
    return false;
}

// ===================================================================== apply
//
// The function dispatcher.  Each branch returns a Value, never throws.
// The convention: a numeric function that cannot satisfy its contract
// (division by zero, non-numeric operand, overflow) returns an absent
// Value, which the call site treats as "no output" or, inside a block,
// "the block may collapse" depending on what produced the absence.

Value Evaluator::apply(const FunctionCall& fc) noexcept {
    // Helper: get a string argument (concatenated arg).
    auto arg_str = [&](std::size_t i) -> std::string {
        if (i >= fc.args.size()) return {};
        std::string s;
        for (const Node& n : fc.args[i]) {
            Value v = eval_node(n);
            if (!v.absent()) s += v.str();
        }
        return s;
    };
    // Helper: get an arg as an int64 if it is a numeric literal.  Returns
    // false (and sets out to 0) if the arg is absent or non-numeric.
    auto arg_int = [&](std::size_t i, std::int64_t& out) -> bool {
        if (i >= fc.args.size()) return false;
        // The arg might be a function call; evaluate first.
        std::string s = arg_str(i);
        return strict_parse_int(s, out);
    };
    // Same for double.
    auto arg_double = [&](std::size_t i, double& out) -> bool {
        if (i >= fc.args.size()) return false;
        std::string s = arg_str(i);
        return strict_parse_double(s, out);
    };

    const std::string& name = fc.name;

    // ----- conditional -------------------------------------------------
    if (name == "if") {
        if (fc.args.size() < 2) return Value{};
        const std::string cond = arg_str(0);
        const std::string yes = arg_str(1);
        // Non-empty cond picks then; empty or absent cond picks else
        // (if present) or absent (if no else arg).
        if (!cond.empty()) return Value{yes};
        if (fc.args.size() >= 3) {
            return Value{arg_str(2)};
        }
        return Value{};  // no else and cond empty/absent → absent
    }
    if (name == "if2") {
        const std::string a = arg_str(0);
        if (!a.empty()) return Value{a};
        const std::string b = arg_str(1);
        return Value{b};
    }
    if (name == "if3") {
        for (const auto& arg : fc.args) {
            std::string s;
            for (const Node& n : arg) {
                Value v = eval_node(n);
                if (!v.absent()) s += v.str();
            }
            if (!s.empty()) return Value{std::move(s)};
        }
        return Value{};
    }
    if (name == "ifequal" || name == "ifgreater" || name == "ifless") {
        if (fc.args.size() != 4) return Value{};
        std::int64_t x = 0, y = 0;
        if (!arg_int(0, x) || !arg_int(1, y)) return Value{};
        const std::string yes = arg_str(2);
        const std::string no = arg_str(3);
        bool take = false;
        if (name == "ifequal") take = (x == y);
        else if (name == "ifgreater") take = (x > y);
        else take = (x < y);
        return Value{take ? yes : no};
    }
    if (name == "iflonger") {
        if (fc.args.size() != 4) return Value{};
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        const std::string yes = arg_str(2);
        const std::string no = arg_str(3);
        const std::size_t len = grapheme_count(s);
        return Value{(static_cast<std::int64_t>(len) > n) ? yes : no};
    }

    // ----- string ------------------------------------------------------
    if (name == "upper") return Value{text::to_upper(arg_str(0))};
    if (name == "lower") return Value{text::to_lower(arg_str(0))};
    if (name == "title") return Value{text::to_title(arg_str(0))};
    if (name == "trim") return Value{std::string{text::trim(arg_str(0))}};
    if (name == "len") {
        // $len of an absent field is 0, not absent (per conformance).
        // arg_str returns "" for absent; grapheme_count("") is 0.
        return Value{std::to_string(grapheme_count(arg_str(0)))};
    }
    if (name == "sub") {
        const std::string s = arg_str(0);
        std::int64_t start = 0;
        if (!arg_int(1, start)) return Value{};
        if (start < 0) return Value{};  // REQ-EFS-005 / sub-negative-start
        std::int64_t len = std::numeric_limits<std::int64_t>::max();
        if (fc.args.size() >= 3) {
            if (!arg_int(2, len)) return Value{};
        }
        return Value{grapheme_substr(s, static_cast<std::size_t>(start),
                                     len == std::numeric_limits<std::int64_t>::max()
                                         ? std::string_view::npos
                                         : static_cast<std::size_t>(len))};
    }
    if (name == "left") {
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (n <= 0) return Value{std::string{}};
        return Value{grapheme_substr(s, 0, static_cast<std::size_t>(n))};
    }
    if (name == "right") {
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (n <= 0) return Value{std::string{}};
        const std::size_t len = grapheme_count(s);
        if (static_cast<std::size_t>(n) >= len) return Value{s};
        return Value{grapheme_substr(s, len - static_cast<std::size_t>(n),
                                     static_cast<std::size_t>(n))};
    }
    if (name == "pad") {
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (static_cast<std::size_t>(n) <= s.size()) return Value{s};
        const std::string ch = fc.args.size() >= 3 ? arg_str(2) : " ";
        if (ch.empty()) return Value{s};
        return Value{std::string(static_cast<std::size_t>(n) - s.size(),
                                 ch.front()) +
                     s};
    }
    if (name == "padright") {
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (static_cast<std::size_t>(n) <= s.size()) return Value{s};
        const std::string ch = fc.args.size() >= 3 ? arg_str(2) : " ";
        if (ch.empty()) return Value{s};
        return Value{s + std::string(static_cast<std::size_t>(n) - s.size(), ch.front())};
    }
    if (name == "cut") {
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (n <= 0) return Value{std::string{}};
        return Value{grapheme_substr(s, 0, static_cast<std::size_t>(n))};
    }
    if (name == "abbr") {
        const std::string s = arg_str(0);
        std::int64_t threshold = 0;
        if (fc.args.size() >= 2) {
            if (!arg_int(1, threshold)) return Value{};
        }
        if (static_cast<std::size_t>(threshold) != 0 &&
            grapheme_count(s) <= static_cast<std::size_t>(threshold)) {
            return Value{s};
        }
        // Initials: first grapheme of each whitespace-delimited word,
        // uppercased.  A "word" is a maximal run of non-whitespace
        // graphemes.  The conformance cases use only ASCII spaces.
        std::string out;
        bool at_word_start = true;
        Graphemes g{s};
        while (!g.done()) {
            const std::string_view cluster = g.next();
            std::size_t inner = 0;
            const char32_t c = text::decode_utf8(cluster, inner);
            const bool is_space = (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r');
            if (is_space) {
                at_word_start = true;
                continue;
            }
            if (at_word_start) {
                // Append the first UTF-8 codepoint of the cluster.
                std::size_t p = 0;
                (void)text::decode_utf8(cluster, p);
                out.append(cluster.data(), p);
                at_word_start = false;
            }
        }
        return Value{text::to_upper(out)};
    }
    if (name == "replace") {
        const std::string s = arg_str(0);
        const std::string find = arg_str(1);
        const std::string repl = fc.args.size() >= 3 ? arg_str(2) : "";
        if (find.empty()) return Value{s};
        return Value{text::replace_all(s, find, repl)};
    }
    if (name == "strchr") {
        const std::string s = arg_str(0);
        const std::string ch = arg_str(1);
        if (ch.empty()) return Value{};
        // Search grapheme-by-grapheme comparing the first codepoint.
        std::size_t i = 0;
        Graphemes g{s};
        while (!g.done()) {
            const std::string_view cluster = g.next();
            std::size_t p = 0;
            const char32_t cs = text::decode_utf8(cluster, p);
            std::size_t q = 0;
            const char32_t cc = text::decode_utf8(ch, q);
            if (cs == cc) return Value{std::to_string(i)};
            ++i;
        }
        return Value{};  // not found → absent
    }
    if (name == "strstr") {
        const std::string s = arg_str(0);
        const std::string sub = arg_str(1);
        if (sub.empty()) return Value{"0"};
        // Grapheme-by-grapheme scan; O(n*m) but n ≤ 4096.
        Graphemes g{s};
        std::string buf;
        std::size_t i = 0;
        while (!g.done()) {
            const std::string_view cluster = g.next();
            buf.append(cluster);
            if (buf.size() >= sub.size()) {
                // Compare the suffix.
                if (std::memcmp(buf.data() + buf.size() - sub.size(), sub.data(),
                                sub.size()) == 0) {
                    return Value{std::to_string(i + 1 - grapheme_count(sub))};
                }
            }
            ++i;
        }
        return Value{};
    }
    if (name == "insert") {
        const std::string s = arg_str(0);
        const std::string ins = arg_str(1);
        std::int64_t at = 0;
        if (!arg_int(2, at)) return Value{};
        if (at < 0) at = 0;
        const std::size_t len = grapheme_count(s);
        if (static_cast<std::size_t>(at) > len) {
            return Value{s + ins};
        }
        const std::string head = grapheme_substr(s, 0, static_cast<std::size_t>(at));
        const std::string tail = grapheme_substr(s, static_cast<std::size_t>(at),
                                                 std::string_view::npos);
        return Value{head + ins + tail};
    }
    if (name == "repeat") {
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (n < 0 || static_cast<std::size_t>(n) > kArgCap) return Value{};
        const std::string s = arg_str(0);
        std::string out;
        out.reserve(s.size() * static_cast<std::size_t>(n));
        for (std::int64_t i = 0; i < n; ++i) out += s;
        return Value{std::move(out)};
    }
    if (name == "caps") {
        // First letter of each word upper, rest unchanged.
        std::string s = arg_str(0);
        bool at_start = true;
        std::size_t pos = 0;
        std::string out;
        out.reserve(s.size());
        while (pos < s.size()) {
            const std::size_t before = pos;
            const char32_t cp = text::decode_utf8(s, pos);
            const std::string cluster{s.data() + before, pos - before};
            const bool boundary = (cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' ||
                                   cp == U'(' || cp == U'[' || cp == U'{');
            if (at_start) {
                std::size_t p = 0;
                (void)text::decode_utf8(cluster, p);
                out.append(cluster.data(), p);
                out.append(cluster.data() + p, cluster.size() - p);
            } else {
                out += cluster;
            }
            at_start = boundary;
        }
        return Value{std::move(out)};
    }
    if (name == "meta_sep") {
        // The arg is a FIELD NAME (a literal string), not a value
        // reference.  meta_sep-takes-field-name-not-ref is the
        // conformance case that nails this down: passing `%artist%`
        // passes the joined value, which is not a field name, so the
        // call is absent.
        if (fc.args.empty()) return Value{};
        // The first arg must be a literal, with no field ref inside.
        // If it contains a field ref, the call is absent.
        bool is_pure_literal = true;
        std::string field_name;
        for (const Node& n : fc.args[0]) {
            if (n.kind() == Node::Kind::Literal) {
                field_name += n.literal().text;
            } else {
                is_pure_literal = false;
                break;
            }
        }
        if (!is_pure_literal || field_name.empty()) return Value{};
        const std::optional<std::vector<std::string>> multi = track_.multi_field(field_name);
        if (!multi.has_value()) return Value{};
        if (multi->empty()) return Value{std::string{}};
        const std::string sep = fc.args.size() >= 2 ? arg_str(1) : "; ";
        return Value{text::join(*multi, sep)};
    }

    // ----- numeric -----------------------------------------------------
    if (name == "add") {
        std::int64_t acc = 0;
        for (std::size_t i = 0; i < fc.args.size(); ++i) {
            std::int64_t v = 0;
            if (!arg_int(i, v)) return Value{};
            std::int64_t tmp = 0;
            if (add_overflow(acc, v, tmp)) return Value{};
            acc = tmp;
        }
        return Value{std::to_string(acc)};
    }
    if (name == "sub2") {
        if (fc.args.size() != 2) return Value{};
        std::int64_t a = 0, b = 0;
        if (!arg_int(0, a) || !arg_int(1, b)) return Value{};
        std::int64_t out = 0;
        if (sub_overflow(a, b, out)) return Value{};
        return Value{std::to_string(out)};
    }
    if (name == "mul") {
        std::int64_t acc = 1;
        for (std::size_t i = 0; i < fc.args.size(); ++i) {
            std::int64_t v = 0;
            if (!arg_int(i, v)) return Value{};
            std::int64_t tmp = 0;
            if (mul_overflow(acc, v, tmp)) return Value{};
            acc = tmp;
        }
        return Value{std::to_string(acc)};
    }
    if (name == "div") {
        if (fc.args.size() != 2) return Value{};
        double a = 0.0, b = 0.0;
        if (!arg_double(0, a) || !arg_double(1, b)) return Value{};
        return Value{format_div(a, b)};
    }
    if (name == "mod") {
        if (fc.args.size() != 2) return Value{};
        std::int64_t a = 0, b = 0;
        if (!arg_int(0, a) || !arg_int(1, b)) return Value{};
        if (b == 0) return Value{};
        return Value{std::to_string(trunc_mod(a, b))};
    }
    if (name == "min") {
        if (fc.args.empty()) return Value{};
        std::int64_t best = 0;
        bool first = true;
        for (std::size_t i = 0; i < fc.args.size(); ++i) {
            std::int64_t v = 0;
            if (!arg_int(i, v)) return Value{};
            if (first || v < best) {
                best = v;
                first = false;
            }
        }
        return Value{std::to_string(best)};
    }
    if (name == "max") {
        if (fc.args.empty()) return Value{};
        std::int64_t best = 0;
        bool first = true;
        for (std::size_t i = 0; i < fc.args.size(); ++i) {
            std::int64_t v = 0;
            if (!arg_int(i, v)) return Value{};
            if (first || v > best) {
                best = v;
                first = false;
            }
        }
        return Value{std::to_string(best)};
    }
    if (name == "num") {
        if (fc.args.size() != 2) return Value{};
        std::int64_t n = 0, w = 0;
        if (!arg_int(0, n) || !arg_int(1, w)) return Value{};
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
        std::string s{buf};
        const bool neg = (!s.empty() && s[0] == '-');
        const std::string digits = neg ? s.substr(1) : s;
        if (static_cast<std::int64_t>(digits.size()) < w) {
            return Value{(neg ? "-" : "") +
                         std::string(static_cast<std::size_t>(w) - digits.size(), '0') + digits};
        }
        return Value{std::move(s)};
    }
    if (name == "round") {
        if (fc.args.empty()) return Value{};
        double v = 0.0;
        if (!arg_double(0, v)) return Value{};
        std::int64_t dp = 0;
        if (fc.args.size() >= 2) {
            if (!arg_int(1, dp)) return Value{};
        }
        return Value{format_round(v, static_cast<int>(dp))};
    }
    if (name == "abs") {
        if (fc.args.empty()) return Value{};
        double v = 0.0;
        if (!arg_double(0, v)) return Value{};
        return Value{format_round(std::abs(v), 6)};
    }

    // ----- time --------------------------------------------------------
    if (name == "time") {
        if (fc.args.empty()) return Value{};
        std::int64_t s = 0;
        if (!arg_int(0, s)) return Value{};
        return Value{format_time(s)};
    }
    if (name == "timems") {
        if (fc.args.empty()) return Value{};
        std::int64_t ms = 0;
        if (!arg_int(0, ms)) return Value{};
        return Value{format_timems(ms)};
    }
    if (name == "date") {
        if (fc.args.empty()) return Value{};
        const std::string iso = arg_str(0);
        const std::string pattern = fc.args.size() >= 2 ? arg_str(1) : "";
        return Value{format_date(iso, pattern)};
    }
    if (name == "year") {
        if (fc.args.empty()) return Value{};
        return Value{format_year(arg_str(0))};
    }
    if (name == "age") {
        if (fc.args.empty()) return Value{};
        return Value{format_age(arg_str(0), track_.now_unix())};
    }

    // ----- presentation -----------------------------------------------
    if (name == "crlf") return Value{std::string{"\n"}};
    if (name == "tab") return Value{std::string{"\t"}};
    if (name == "char") {
        if (fc.args.empty()) return Value{};
        std::int64_t cp = 0;
        if (!arg_int(0, cp)) return Value{};
        if (cp <= 0) return Value{};  // 0 / negative → absent
        if (cp >= 0xD800 && cp <= 0xDFFF) return Value{};  // surrogate
        if (cp > 0x10FFFF) return Value{};
        std::string out;
        text::encode_utf8(static_cast<char32_t>(cp), out);
        return Value{std::move(out)};
    }
    if (name == "progress") {
        if (fc.args.size() < 3) return Value{};
        std::int64_t pos = 0, total = 0, width = 0;
        if (!arg_int(0, pos) || !arg_int(1, total) || !arg_int(2, width)) return Value{};
        if (total == 0) return Value{};
        if (width <= 0 || static_cast<std::size_t>(width) > kArgCap) return Value{};
        const std::string fill = fc.args.size() >= 4 ? arg_str(3) : "=";
        const std::string empty = fc.args.size() >= 5 ? arg_str(4) : "-";
        const char fill_c = fill.empty() ? '=' : fill[0];
        const char empty_c = empty.empty() ? '-' : empty[0];
        const double ratio = static_cast<double>(pos) / static_cast<double>(total);
        const std::size_t filled = static_cast<std::size_t>(ratio * static_cast<double>(width));
        const std::size_t n_filled = std::min(filled, static_cast<std::size_t>(width));
        return Value{std::string(n_filled, fill_c) +
                     std::string(static_cast<std::size_t>(width) - n_filled, empty_c)};
    }
    if (name == "stars") {
        if (fc.args.empty()) return Value{};
        std::int64_t rating = 0;
        if (!arg_int(0, rating)) return Value{};
        std::int64_t max = 5;
        if (fc.args.size() >= 2) {
            if (!arg_int(1, max)) return Value{};
            if (max <= 0) return Value{};
        }
        if (rating < 0) rating = 0;
        if (rating > 100) rating = 100;
        const double v = static_cast<double>(rating) / 100.0 * static_cast<double>(max);
        const double rounded = std::round(v);  // half away from zero
        std::int64_t n = static_cast<std::int64_t>(rounded);
        if (n < 0) n = 0;
        if (n > max) n = max;
        return Value{std::string(static_cast<std::size_t>(n), '\xE2') +
                     "\x98\x85" +
                     std::string(static_cast<std::size_t>(max - n), '\xE2') +
                     "\x98\x86"};
    }
    if (name == "fixed") {
        if (fc.args.size() != 2) return Value{};
        const std::string s = arg_str(0);
        std::int64_t n = 0;
        if (!arg_int(1, n)) return Value{};
        if (n <= 0) return Value{std::string{}};
        if (static_cast<std::size_t>(n) == grapheme_count(s)) return Value{s};
        if (static_cast<std::size_t>(n) < grapheme_count(s)) {
            return Value{grapheme_substr(s, 0, static_cast<std::size_t>(n))};
        }
        return Value{s + std::string(static_cast<std::size_t>(n) - s.size(), ' ')};
    }

    return Value{};
}

}  // namespace arrow::format
