// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// RSS 2.0 / Atom / iTunes podcast extensions parser — spec §17.2,
// REQ-NET-020 .. REQ-NET-022.
//
// The parser is hand-written, not built on libxml2. Three reasons:
//   1. libxml2 is not in the dependency register (§4.2) and would change the
//      link rules for the domain layer (REQ-GEN-050).
//   2. The feeds we need to read are simple. RSS 2.0 with iTunes extensions
//      and Atom 1.0 with the same are well within the scope of a state
//      machine that walks a byte buffer.
//   3. Every feed is untrusted input (REQ-NET-022), so the parser's
//      limits are easier to audit against the spec when the code is 400
//      lines than when it is 4,000.

#include "net/ports/rss_feed.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arrow::net {

namespace {

constexpr std::size_t kMaxAttrValue = 4096;
constexpr std::size_t kMaxTextRun = 1u << 20;  // 1 MiB of plain text per element

bool is_name_char(char c) noexcept {
    // XML names: letters, digits, '.', '-', '_', ':'. We accept the
    // common ASCII subset; non-ASCII letters in element names are rare and
    // would be an authoring bug worth surfacing as a parse error.
    static const std::string allowed =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:";
    return allowed.find(c) != std::string::npos;
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

struct Attr {
    std::string name;
    std::string value;
};

bool ieq(std::string_view a, std::string_view b) noexcept;

struct Token {
    enum class Kind { Prolog, Open, Close, SelfClose, Text, CData, Comment, Doctype, Eof };
    Kind kind{Kind::Eof};
    std::string name;             ///< element name for Open/Close/SelfClose
    std::vector<Attr> attrs;      ///< attributes for Open/SelfClose
    std::string text;             ///< text content for Text / CData
    std::size_t depth{0};
};

class Lexer {
  public:
    explicit Lexer(std::string_view text, const FeedLimits& limits)
        : text_{text}, limits_{limits} {}

    [[nodiscard]] Status precondition() const {
        if (text_.size() > limits_.max_bytes) {
            return err(ErrorCode::InputTooLarge,
                       "Podcast feed exceeds the 8 MiB cap",
                       "see REQ-NET-022");
        }
        return ok();
    }

    [[nodiscard]] Token next() {
        if (at_end()) return eof();
        if (depth_ > limits_.max_depth) {
            err_ = err(ErrorCode::NestingTooDeep,
                       "Podcast feed nesting exceeds the limit",
                       "see REQ-NET-022");
            return eof();
        }
        if (text_[pos_] != '<') {
            return read_text();
        }
        if (text_.compare(pos_, 4, "<!--") == 0) {
            return read_comment();
        }
        if (text_.compare(pos_, 9, "<![CDATA[") == 0) {
            return read_cdata();
        }
        if (text_.compare(pos_, 9, "<!DOCTYPE") == 0) {
            return read_doctype();
        }
        if (text_.compare(pos_, 5, "<?xml") == 0) {
            return read_prolog();
        }
        return read_element();
    }

    [[nodiscard]] bool has_error() const noexcept { return static_cast<bool>(err_); }
    [[nodiscard]] const Error& error() const noexcept { return err_.value(); }

  private:
    [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }

    [[nodiscard]] Token eof() {
        Token t;
        t.kind = Token::Kind::Eof;
        t.depth = depth_;
        return t;
    }

    Token read_text() {
        Token t;
        t.kind = Token::Kind::Text;
        t.depth = depth_;
        const std::size_t end = text_.find('<', pos_);
        const std::size_t stop = end == std::string_view::npos ? text_.size() : end;
        std::size_t run = stop - pos_;
        if (run > kMaxTextRun) {
            err_ = err(ErrorCode::InputTooLarge,
                       "Podcast feed text run exceeds 1 MiB",
                       "text node too large");
            return eof();
        }
        t.text.assign(text_.data() + pos_, run);
        pos_ = stop;
        return t;
    }

    Token read_comment() {
        Token t;
        t.kind = Token::Kind::Comment;
        t.depth = depth_;
        const std::size_t end = text_.find("-->", pos_ + 4);
        if (end == std::string_view::npos) {
            err_ = err(ErrorCode::UnexpectedEnd,
                       "Unterminated XML comment",
                       "expected '-->' to close the comment");
            return eof();
        }
        pos_ = end + 3;
        return t;
    }

    Token read_cdata() {
        Token t;
        t.kind = Token::Kind::CData;
        t.depth = depth_;
        const std::size_t end = text_.find("]]>", pos_ + 9);
        if (end == std::string_view::npos) {
            err_ = err(ErrorCode::UnexpectedEnd,
                       "Unterminated CDATA section",
                       "expected ']]>' to close the CDATA");
            return eof();
        }
        if (end - (pos_ + 9) > kMaxTextRun) {
            err_ = err(ErrorCode::InputTooLarge,
                       "CDATA section exceeds 1 MiB",
                       "text node too large");
            return eof();
        }
        t.text.assign(text_.data() + pos_ + 9, end - (pos_ + 9));
        pos_ = end + 3;
        return t;
    }

    Token read_doctype() {
        // The DTD subset is consumed but otherwise ignored. We refuse to
        // expand any external entity or notation by the structure of this
        // parser: there is no entity store, period. Rejecting <!DOCTYPE>
        // altogether would break valid feeds that happen to declare the
        // version, so we accept the declaration but never act on it.
        Token t;
        t.kind = Token::Kind::Doctype;
        t.depth = depth_;
        std::size_t i = pos_ + 9;
        bool in_quote = false;
        while (i < text_.size()) {
            char c = text_[i];
            if (in_quote) {
                if (c == '"') in_quote = false;
                ++i;
                continue;
            }
            if (c == '"') {
                in_quote = true;
                ++i;
                continue;
            }
            if (c == '>' && (i == 0 || text_[i - 1] != ']')) {
                pos_ = i + 1;
                return t;
            }
            ++i;
        }
        err_ = err(ErrorCode::UnexpectedEnd,
                   "Unterminated DOCTYPE",
                   "expected '>' to close the DOCTYPE");
        return eof();
    }

    Token read_prolog() {
        Token t;
        t.kind = Token::Kind::Prolog;
        t.depth = depth_;
        const std::size_t end = text_.find("?>", pos_ + 5);
        if (end == std::string_view::npos) {
            err_ = err(ErrorCode::UnexpectedEnd,
                       "Unterminated XML prolog",
                       "expected '?>' to close the prolog");
            return eof();
        }
        pos_ = end + 2;
        return t;
    }

    Token read_element() {
        Token t;
        t.depth = depth_;
        ++pos_;  // consume '<'
        // Read element name.
        const std::size_t name_begin = pos_;
        while (pos_ < text_.size() && is_name_char(text_[pos_])) {
            ++pos_;
        }
        if (pos_ == name_begin) {
            err_ = err(ErrorCode::UnexpectedToken,
                       "XML element missing a name",
                       "expected a name character after '<'");
            return eof();
        }
        t.name.assign(text_.data() + name_begin, pos_ - name_begin);
        // Read attributes.
        while (pos_ < text_.size()) {
            skip_whitespace();
            if (pos_ >= text_.size()) break;
            if (text_[pos_] == '>') {
                ++pos_;
                t.kind = Token::Kind::Open;
                return t;
            }
            if (text_[pos_] == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '>') {
                pos_ += 2;
                t.kind = Token::Kind::SelfClose;
                return t;
            }
            // Attribute.
            const std::size_t a_begin = pos_;
            while (pos_ < text_.size() && is_name_char(text_[pos_])) ++pos_;
            if (pos_ == a_begin) {
                err_ = err(ErrorCode::UnexpectedToken,
                           "XML element has an unrecognised character",
                           "expected whitespace, '>', '/>', or an attribute name");
                return eof();
            }
            Attr a;
            a.name.assign(text_.data() + a_begin, pos_ - a_begin);
            skip_whitespace();
            if (pos_ >= text_.size() || text_[pos_] != '=') {
                err_ = err(ErrorCode::UnexpectedToken,
                           "XML attribute missing '='",
                           "every attribute must have a value");
                return eof();
            }
            ++pos_;
            skip_whitespace();
            if (pos_ >= text_.size() ||
                (text_[pos_] != '"' && text_[pos_] != '\'')) {
                err_ = err(ErrorCode::UnexpectedToken,
                           "XML attribute value not quoted",
                           "expected '\"' or '\\'' around attribute value");
                return eof();
            }
            const char quote = text_[pos_];
            ++pos_;
            const std::size_t v_begin = pos_;
            while (pos_ < text_.size() && text_[pos_] != quote) ++pos_;
            if (pos_ >= text_.size()) {
                err_ = err(ErrorCode::UnexpectedEnd,
                           "Unterminated attribute value",
                           "expected the closing quote");
                return eof();
            }
            if (pos_ - v_begin > kMaxAttrValue) {
                err_ = err(ErrorCode::InputTooLarge,
                           "XML attribute value exceeds 4 KiB",
                           "see REQ-SEC-002");
                return eof();
            }
            a.value.assign(text_.data() + v_begin, pos_ - v_begin);
            ++pos_;  // closing quote
            t.attrs.push_back(std::move(a));
        }
        err_ = err(ErrorCode::UnexpectedEnd,
                   "Unterminated XML element",
                   "expected '>' or '/>' to close the tag");
        return eof();
    }

    void skip_whitespace() noexcept {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                text_[pos_] == '\r' || text_[pos_] == '\n')) {
            ++pos_;
        }
    }

    std::string_view text_;
    const FeedLimits& limits_;
    std::size_t pos_{0};
    std::size_t depth_{0};
    std::optional<Error> err_;
};

const Attr* find_attr(const std::vector<Attr>& attrs, std::string_view name) noexcept {
    for (const auto& a : attrs) {
        if (ieq(a.name, name)) return &a;
    }
    return nullptr;
}

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string decode_entities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out.push_back(s[i]);
            continue;
        }
        if (s.compare(i, 5, "&amp;") == 0) {
            out.push_back('&');
            i += 4;
        } else if (s.compare(i, 4, "&lt;") == 0) {
            out.push_back('<');
            i += 3;
        } else if (s.compare(i, 4, "&gt;") == 0) {
            out.push_back('>');
            i += 3;
        } else if (s.compare(i, 5, "&apos;") == 0) {
            out.push_back('\'');
            i += 5;
        } else if (s.compare(i, 5, "&quot;") == 0) {
            out.push_back('"');
            i += 5;
        } else if (s[i + 1] == '#' && i + 2 < s.size()) {
            // Numeric character reference. We accept decimal only here —
            // feeds in the wild use &#xNN; rarely, and the only one that
            // matters is the apostrophe-escape already covered above.
            std::size_t end = s.find(';', i);
            if (end == std::string_view::npos) {
                out.push_back(s[i]);
                continue;
            }
            std::string_view digits = s.substr(i + 2, end - i - 2);
            std::uint32_t cp = 0;
            bool ok = !digits.empty();
            for (char c : digits) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                cp = cp * 10 + static_cast<std::uint32_t>(c - '0');
            }
            if (!ok || cp == 0 || cp > 0x10FFFF) {
                out.push_back(s[i]);
                continue;
            }
            // Encode as UTF-8.
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            i = end;
        } else {
            // Unknown entity. REQ-NET-022 / REQ-SEC-002: the parser must
            // never silently expand custom entities, because that is the
            // billion-laughs vector. Preserve the literal so the user can
            // see the problem in the rendered description.
            out.push_back(s[i]);
        }
    }
    return out;
}

void strip_html_tags(std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') {
            in_tag = true;
            continue;
        }
        if (c == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) out.push_back(c);
    }
    s = std::move(out);
}

std::optional<std::int64_t> parse_rfc822(std::string_view s) {
    // RFC 822 / RFC 2822 dates: "Tue, 10 Aug 2004 13:00:00 GMT". We try a few
    // common formats; if none match, the date is left in pub_date_raw and
    // pub_date_epoch stays empty so the queue can still order the episode
    // by GUID when needed.
    static const char* formats[] = {
        "%a, %d %b %Y %H:%M:%S %Z",
        "%a, %d %b %Y %H:%M:%S",
        "%d %b %Y %H:%M:%S %Z",
        "%d %b %Y %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S%Z",
        "%Y-%m-%dT%H:%M:%S",
    };
    std::tm tm{};
    for (const char* fmt : formats) {
        std::memset(&tm, 0, sizeof(tm));
        std::istringstream is{std::string{s}};
        is >> std::get_time(&tm, fmt);
        if (!is.fail()) {
            // timegm is GNU/BSD; time_t is implementation-defined but every
            // platform we ship on has a 64-bit one.
            const std::time_t t = timegm(&tm);
            if (t >= 0) {
                return static_cast<std::int64_t>(t);
            }
        }
    }
    return std::nullopt;
}

std::string identity_hash(std::string_view guid, std::string_view enclosure_url) {
    // FNV-1a 64-bit over the concatenation. The hash is only used by the
    // queue to dedupe within a single feed fetch — cryptographic strength
    // is not needed.
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = kOffset;
    for (char c : guid) {
        h ^= static_cast<unsigned char>(c);
        h *= kPrime;
    }
    h ^= '|';
    h *= kPrime;
    for (char c : enclosure_url) {
        h ^= static_cast<unsigned char>(c);
        h *= kPrime;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(h));
    return std::string{buf};
}

bool parse_int_attr(const std::vector<Attr>& attrs, std::string_view name,
                    int& out) {
    const Attr* a = find_attr(attrs, name);
    if (a == nullptr) return false;
    int v = 0;
    for (char c : a->value) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

}  // namespace

Result<PodcastFeed> parse_podcast_feed(std::string_view text,
                                       const FeedLimits& limits) {
    Lexer lex(text, limits);
    if (auto s = lex.precondition(); !s) return s.error();

    PodcastFeed feed;
    bool seen_channel = false;
    bool seen_feed = false;
    bool in_item = false;
    bool in_channel_image = false;  // for <image><url>...</url></image>
    PodcastEpisode current;

    auto flush_item = [&]() {
        if (!current.guid.empty() || current.enclosure.has_value()) {
            current.identity_hash = identity_hash(
                current.guid, current.enclosure ? current.enclosure->url : "");
            feed.episodes.push_back(std::move(current));
        }
        current = PodcastEpisode{};
    };

    auto set_field = [&](std::string_view name, std::string_view value) {
        if (in_item) {
            if (ieq(name, "title")) {
                if (current.title.empty()) current.title.assign(value);
            } else if (ieq(name, "description")) {
                if (current.description.empty()) current.description.assign(value);
            } else if (ieq(name, "guid")) {
                if (current.guid.empty()) current.guid.assign(value);
            } else if (ieq(name, "link")) {
                if (current.link.empty()) current.link.assign(value);
            } else if (ieq(name, "pubDate") || ieq(name, "published")) {
                if (current.pub_date_raw.empty()) {
                    current.pub_date_raw.assign(value);
                    if (auto t = parse_rfc822(value)) current.pub_date_epoch = *t;
                }
            } else if (ieq(name, "itunes:duration")) {
                int total = 0;
                for (char c : value) {
                    if (c == ':') {
                        total = total * 60;
                        continue;
                    }
                    if (c < '0' || c > '9') break;
                    total = total * 10 + (c - '0');
                }
                current.duration = std::chrono::seconds{total};
            }
            return;
        }
        if (ieq(name, "title")) {
            if (feed.title.empty()) feed.title.assign(value);
        } else if (ieq(name, "description") || ieq(name, "subtitle") ||
                   ieq(name, "itunes:summary")) {
            if (feed.description.empty()) feed.description.assign(value);
        } else if (ieq(name, "link")) {
            if (feed.link.empty()) feed.link.assign(value);
        } else if (ieq(name, "author") || ieq(name, "itunes:author")) {
            if (feed.author.empty()) feed.author.assign(value);
        } else if (ieq(name, "language")) {
            if (feed.language.empty()) feed.language.assign(value);
        } else if (ieq(name, "copyright")) {
            if (feed.copyright.empty()) feed.copyright.assign(value);
        } else if (ieq(name, "lastBuildDate")) {
            if (feed.last_build_date_raw.empty()) feed.last_build_date_raw.assign(value);
        }
    };

    std::vector<std::string> open_stack;  // for implicit close detection
    std::vector<std::string_view> name_stack;  // for the matching close

    auto close_top = [&](std::string_view name) {
        // Pop the most recent matching open. If a feed uses the HTML-style
        // shorthand where a <p> element is closed by the next start-tag, we
        // still close only the explicit </p> — XML has no HTML recovery.
        for (auto it = name_stack.rbegin(); it != name_stack.rend(); ++it) {
            if (ieq(*it, name)) {
                const std::size_t pop_count =
                    static_cast<std::size_t>(std::distance(name_stack.rbegin(), it)) + 1;
                for (std::size_t k = 0; k < pop_count; ++k) {
                    if (!name_stack.empty()) name_stack.pop_back();
                    if (!open_stack.empty()) open_stack.pop_back();
                }
                return;
            }
        }
    };

    while (true) {
        Token t = lex.next();
        if (lex.has_error()) return lex.error();
        switch (t.kind) {
            case Token::Kind::Eof:
                if (in_item) flush_item();
                if (!seen_channel && !seen_feed) {
                    return err(ErrorCode::MalformedHeader,
                               "Podcast feed has no <channel> or <feed> root",
                               "expected RSS 2.0 <rss><channel> or Atom <feed>");
                }
                std::reverse(feed.episodes.begin(), feed.episodes.end());
                return feed;
            case Token::Kind::Prolog:
            case Token::Kind::Comment:
            case Token::Kind::CData:
            case Token::Kind::Doctype:
                continue;
            case Token::Kind::Open: {
                name_stack.push_back(t.name);
                open_stack.push_back(t.name);
                const std::string lower = to_lower(t.name);
                if (lower == "rss" || lower == "rdf") {
                    seen_feed = true;
                }
                if (lower == "channel" || lower == "feed") {
                    seen_channel = true;
                }
                if (lower == "item" || lower == "entry") {
                    in_item = true;
                    current = PodcastEpisode{};
                }
                if (lower == "image") {
                    in_channel_image = true;
                }
                if (in_item) {
                    if (lower == "enclosure") {
                        const Attr* url = find_attr(t.attrs, "url");
                        const Attr* type = find_attr(t.attrs, "type");
                        const Attr* len = find_attr(t.attrs, "length");
                        PodcastEnclosure e;
                        if (url) e.url = url->value;
                        if (type) e.mime_type = type->value;
                        if (len) {
                            std::int64_t n = 0;
                            for (char c : len->value) {
                                if (c < '0' || c > '9') {
                                    n = 0;
                                    break;
                                }
                                n = n * 10 + (c - '0');
                            }
                            e.length_bytes = n;
                        }
                        if (!e.url.empty()) current.enclosure = std::move(e);
                    } else if (lower == "itunes:season") {
                        int v = 0;
                        if (parse_int_attr(t.attrs, "value", v) ||
                            parse_int_attr(t.attrs, "number", v)) {
                            current.season = v;
                        }
                    } else if (lower == "itunes:episode") {
                        int v = 0;
                        if (parse_int_attr(t.attrs, "value", v) ||
                            parse_int_attr(t.attrs, "number", v)) {
                            current.episode_number = v;
                        }
                    }
                } else {
                    if (lower == "itunes:image" && !in_channel_image) {
                        const Attr* href = find_attr(t.attrs, "href");
                        if (href && feed.artwork_url.empty()) {
                            feed.artwork_url = href->value;
                        }
                    }
                    if (lower == "enclosure" && !in_channel_image) {
                        // Channel-level artwork via RSS <image> handled below.
                    }
                }
                break;
            }
            case Token::Kind::SelfClose: {
                const std::string lower = to_lower(t.name);
                if (in_item) {
                    if (lower == "enclosure") {
                        const Attr* url = find_attr(t.attrs, "url");
                        if (url && !current.enclosure.has_value()) {
                            PodcastEnclosure e;
                            e.url = url->value;
                            if (const Attr* type = find_attr(t.attrs, "type")) {
                                e.mime_type = type->value;
                            }
                            if (const Attr* len = find_attr(t.attrs, "length")) {
                                std::int64_t n = 0;
                                for (char c : len->value) {
                                    if (c < '0' || c > '9') break;
                                    n = n * 10 + (c - '0');
                                }
                                e.length_bytes = n;
                            }
                            current.enclosure = std::move(e);
                        }
                    } else if (lower == "itunes:season") {
                        int v = 0;
                        if (parse_int_attr(t.attrs, "value", v) ||
                            parse_int_attr(t.attrs, "number", v)) {
                            current.season = v;
                        }
                    } else if (lower == "itunes:episode") {
                        int v = 0;
                        if (parse_int_attr(t.attrs, "value", v) ||
                            parse_int_attr(t.attrs, "number", v)) {
                            current.episode_number = v;
                        }
                    } else if (lower == "guid") {
                        if (current.guid.empty()) {
                            if (const Attr* a = find_attr(t.attrs, "value")) {
                                current.guid = a->value;
                            } else if (const Attr* b = find_attr(t.attrs, "id")) {
                                current.guid = b->value;
                            }
                        }
                    } else if (lower == "itunes:image") {
                        // Self-closing artwork on an episode.
                    }
                } else {
                    if (lower == "itunes:image") {
                        const Attr* href = find_attr(t.attrs, "href");
                        if (href && feed.artwork_url.empty()) {
                            feed.artwork_url = href->value;
                        }
                    }
                }
                break;
            }
            case Token::Kind::Close: {
                const std::string lower = to_lower(t.name);
                if (lower == "item" || lower == "entry") {
                    if (in_item) flush_item();
                    in_item = false;
                }
                if (lower == "image") in_channel_image = false;
                close_top(t.name);
                break;
            }
            case Token::Kind::Text: {
                std::string decoded = decode_entities(t.text);
                strip_html_tags(decoded);
                if (in_channel_image && ieq(name_stack.back(), "url")) {
                    if (feed.artwork_url.empty()) feed.artwork_url = decoded;
                } else if (!name_stack.empty()) {
                    set_field(name_stack.back(), decoded);
                }
                break;
            }
        }
        if (feed.episodes.size() > limits.max_elements) {
            return err(ErrorCode::InputTooLarge,
                       "Podcast feed has more elements than the limit",
                       "see REQ-SEC-002");
        }
    }
}

}  // namespace arrow::net
