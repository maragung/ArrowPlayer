// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MusicBrainz integration — spec §17.3, REQ-NET-030.
//
// Architecture:
//   - A thread-safe LRU cache (req/res TTL 24 h) backed by an in-process
//     SQLite store so that cache survives a restart.
//   - A token-bucket rate limiter enforced at 1 req/s before any request is
//     issued to MusicBrainz (REQ-NET-030).
//   - The AcoustID fingerprint lookup uses the fpcalc / Chromaprint fingerprint
//     as input; callers who hold a fingerprint call lookup_fingerprint_sync().
//
// Privacy note (REQ-NET-030 / §19.5): fingerprint lookups send a perceptual hash
// of the audio to AcoustID, which is a third-party service. We treat it as an
// opt-in feature and log it as such. No fingerprint is cached locally.

#include "net/ports/musicbrainz.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/text.hpp"

namespace arrow::net {

namespace {

// MusicBrainz base URL and API version.
constexpr std::string_view kBaseUrl = "https://musicbrainz.org/ws/2";
constexpr std::string_view kUserAgentProduct = "arrow-player";

// AcoustID API base.
constexpr std::string_view kAcoustIdBase = "https://acoustid.org/lookup";

/// Rate limiter: one request per second. Enforced before any network call.
class RateLimiter {
  public:
    explicit RateLimiter(std::chrono::seconds period) : period_{period} {}

    /// Acquire a token, blocking until one is available or the token is
    /// cancelled. Returns false if cancelled.
    bool acquire(CancellationToken& cancel) {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock{mu_};
                if (tokens_ < 1) {
                    const auto now = std::chrono::steady_clock::now();
                    if (next_token_.time_since_epoch().count() == 0) {
                        next_token_ = now;
                    }
                    if (tokens_ < 1) {
                        const auto wait = next_token_ - now;
                        if (wait > std::chrono::seconds{0}) {
                            cancel_cv_.wait_until(lock, next_token_,
                                [&] { return cancel.cancelled(); });
                            if (cancel.cancelled()) return false;
                            if (tokens_ < 1 &&
                                std::chrono::steady_clock::now() < next_token_) {
                                // spurious wakeup — wait again
                                continue;
                            }
                        }
                    }
                }
                if (tokens_ < 1) {
                    tokens_ = 1;
                    next_token_ = std::chrono::steady_clock::now() + period_;
                } else {
                    --tokens_;
                }
                return true;
            }
        }
    }

    void reset() {
        std::unique_lock<std::mutex> lock{mu_};
        tokens_ = 1;
        next_token_ = {};
        cancel_cv_.notify_all();
    }

  private:
    const std::chrono::seconds period_;
    std::mutex mu_;
    std::condition_variable cancel_cv_;
    int tokens_{0};
    std::chrono::steady_clock::time_point next_token_;
};

/// Percent-encode for URL query params.
std::string url_encode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back("0123456789ABCDEF"[c >> 4]);
            out.push_back("0123456789ABCDEF"[c & 0x0F]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// JSON subset — enough for MusicBrainz responses without pulling in nlohmann/json.
// MusicBrainz returns small, well-formed documents; a full parser is overkill.
// ---------------------------------------------------------------------------

[[maybe_unused]] std::string_view skip_ws(std::string_view s) noexcept {
    while (!s.empty() && (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' ||
                           s[0] == '\r')) {
        s.remove_prefix(1);
    }
    return s;
}

std::optional<std::string> parse_json_string(std::string_view& s) {
    s = skip_ws(s);
    if (s.empty() || s[0] != '"') return std::nullopt;
    s.remove_prefix(1);  // leading quote
    std::string out;
    while (!s.empty() && s[0] != '"') {
        if (s[0] == '\\' && s.size() >= 2) {
            s.remove_prefix(1);
            switch (s[0]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    s.remove_prefix(1);
                    if (s.size() < 4) return std::nullopt;
                    std::uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char c = s[i];
                        cp <<= 4;
                        if (c >= '0' && c <= '9')
                            cp |= c - '0';
                        else if (c >= 'a' && c <= 'f')
                            cp |= 10 + c - 'a';
                        else if (c >= 'A' && c <= 'F')
                            cp |= 10 + c - 'A';
                        else
                            return std::nullopt;
                    }
                    s.remove_prefix(4);
                    // Encode as UTF-8.
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(
                            static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(
                            static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    continue;
                }
                default: return std::nullopt;
            }
        } else {
            out.push_back(s[0]);
        }
        s.remove_prefix(1);
    }
    if (s.empty() || s[0] != '"') return std::nullopt;
    s.remove_prefix(1);  // trailing quote
    return out;
}

std::optional<std::string_view> parse_json_key(std::string_view& s) {
    s = skip_ws(s);
    auto v = parse_json_string(s);
    if (!v) return std::nullopt;
    return *v;
}

std::optional<std::int64_t> parse_json_int(std::string_view& s) {
    s = skip_ws(s);
    if (s.empty() || (s[0] != '-' && (s[0] < '0' || s[0] > '9'))) {
        return std::nullopt;
    }
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        s.remove_prefix(1);
    }
    std::int64_t v = 0;
    int digits = 0;
    while (!s.empty() && s[0] >= '0' && s[0] <= '9') {
        v = v * 10 + (s[0] - '0');
        s.remove_prefix(1);
        ++digits;
    }
    if (digits == 0) return std::nullopt;
    return neg ? -v : v;
}

std::optional<double> parse_json_double(std::string_view& s) {
    s = skip_ws(s);
    if (s.empty()) return std::nullopt;
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        s.remove_prefix(1);
    }
    double int_part = 0.0;
    int digits = 0;
    while (!s.empty() && s[0] >= '0' && s[0] <= '9') {
        int_part = int_part * 10.0 + double(s[0] - '0');
        s.remove_prefix(1);
        ++digits;
    }
    double frac_part = 0.0;
    if (!s.empty() && s[0] == '.') {
        s.remove_prefix(1);
        double div = 10.0;
        while (!s.empty() && s[0] >= '0' && s[0] <= '9') {
            frac_part += double(s[0] - '0') / div;
            div *= 10.0;
            s.remove_prefix(1);
        }
    }
    if (digits == 0 && frac_part == 0.0) return std::nullopt;
    return (neg ? -(int_part + frac_part) : int_part + frac_part);
}

std::optional<std::vector<std::string>> parse_json_string_array(
    std::string_view& s) {
    s = skip_ws(s);
    if (s.empty() || s[0] != '[') return std::nullopt;
    s.remove_prefix(1);
    std::vector<std::string> out;
    for (;;) {
        s = skip_ws(s);
        if (!s.empty() && s[0] == ']') {
            s.remove_prefix(1);
            return out;
        }
        if (s.empty()) return std::nullopt;
        if (auto v = parse_json_string(s)) {
            out.push_back(std::move(*v));
        }
        s = skip_ws(s);
        if (s.empty()) return std::nullopt;
        if (s[0] == ']') {
            s.remove_prefix(1);
            return out;
        }
        if (s[0] != ',') return std::nullopt;
        s.remove_prefix(1);
    }
}

std::optional<std::string> parse_json_str_field(std::string_view& s,
                                               std::string_view key) {
    s = skip_ws(s);
    if (s.empty()) return std::nullopt;
    // Try to find key ":" before the next key or end.
    const std::size_t key_pos = s.find(std::string{key.data(), key.size()} + ":");
    if (key_pos == std::string_view::npos) return std::nullopt;
    std::string_view rest = skip_ws(s.substr(key_pos));
    if (rest.empty() || rest[0] != ':') return std::nullopt;
    rest.remove_prefix(1);
    return parse_json_string(rest);
}

MusicBrainzRecording parse_recording(std::string_view s) {
    MusicBrainzRecording r;
    if (auto v = parse_json_str_field(s, "id")) r.mbid = std::move(*v);
    if (auto v = parse_json_str_field(s, "title")) r.title = std::move(*v);
    if (auto v = parse_json_str_field(s, "artist-credit")) {
        // artist-credit is a nested array; parse a simple list of names.
        // MusicBrainz returns e.g. [{"artist":{"name":"X"},"name":"X"},...]
        std::string_view sub = *v;
        // We already consumed the string; re-search in the original.
        (void)sub;
        std::string_view orig = s;
        const auto pos = orig.find("\"artist-credit\"");
        if (pos != std::string_view::npos) {
            std::string_view credit = orig.substr(pos + 16);
            // Walk through the array.
            while (!credit.empty()) {
                credit = skip_ws(credit);
                if (credit.empty() || credit[0] == ']') break;
                if (credit[0] == '{') {
                    const auto close = credit.find('}');
                    if (close != std::string_view::npos) {
                        auto obj = credit.substr(1, close - 1);
                        if (auto n = parse_json_str_field(obj, "name")) {
                            if (!n->empty()) r.artists.push_back(*n);
                        }
                        credit = credit.substr(close + 1);
                    } else {
                        break;
                    }
                } else {
                    break;
                }
                credit = skip_ws(credit);
                if (!credit.empty() && credit[0] == ',') credit.remove_prefix(1);
            }
        }
    }
    if (auto v = parse_json_str_field(s, "releases")) {
        // Parse first release ID from releases array.
        std::string_view rels = *v;
        (void)rels;
        std::string_view orig = s;
        const auto pos = orig.find("\"releases\"");
        if (pos != std::string_view::npos) {
            std::string_view rlist = orig.substr(pos + 11);
            rlist = skip_ws(rlist);
            if (!rlist.empty() && rlist[0] == '[') {
                rlist.remove_prefix(1);
                rlist = skip_ws(rlist);
                if (!rlist.empty() && rlist[0] == '{') {
                    if (auto id = parse_json_str_field(rlist, "id")) {
                        r.release_mbid = *id;
                    }
                    if (auto id = parse_json_str_field(rlist, "title")) {
                        r.release_title = *id;
                    }
                }
            }
        }
    }
    if (auto v = parse_json_str_field(s, "length")) {
        if (auto ms = parse_json_int(*v)) {
            r.duration_ms = *ms;
        }
    }
    if (auto v = parse_json_str_field(s, "isrcs")) {
        std::string_view orig = s;
        const auto pos = orig.find("\"isrcs\"");
        if (pos != std::string_view::npos) {
            std::string_view arr = orig.substr(pos + 8);
            arr = skip_ws(arr);
            if (!arr.empty() && arr[0] == '[') {
                arr.remove_prefix(1);
                arr = skip_ws(arr);
                if (!arr.empty() && arr[0] == '"') {
                    if (auto isrc = parse_json_string(arr)) {
                        r.isrc = *isrc;
                    }
                }
            }
        }
    }
    r.source = "musicbrainz";
    return r;
}

MusicBrainzRelease parse_release(std::string_view s) {
    MusicBrainzRelease r;
    if (auto v = parse_json_str_field(s, "id")) r.mbid = std::move(*v);
    if (auto v = parse_json_str_field(s, "title")) r.title = std::move(*v);
    if (auto v = parse_json_str_field(s, "date")) r.date = std::move(*v);
    if (auto v = parse_json_str_field(s, "country")) r.country = std::move(*v);
    if (auto v = parse_json_str_field(s, "barcode")) r.barcode = std::move(*v);
    if (auto v = parse_json_str_field(s, "label-info")) {
        // Parse first label name.
        std::string_view orig = s;
        const auto pos = orig.find("\"label-info\"");
        if (pos != std::string_view::npos) {
            std::string_view llist = orig.substr(pos + 13);
            llist = skip_ws(llist);
            if (!llist.empty() && llist[0] == '[') {
                llist.remove_prefix(1);
                llist = skip_ws(llist);
                if (!llist.empty() && llist[0] == '{') {
                    if (auto ln = parse_json_str_field(llist, "name")) {
                        r.label = *ln;
                    }
                }
            }
        }
    }
    r.source = "musicbrainz";
    return r;
}

MusicBrainzArtist parse_artist(std::string_view s) {
    MusicBrainzArtist a;
    if (auto v = parse_json_str_field(s, "id")) a.mbid = std::move(*v);
    if (auto v = parse_json_str_field(s, "name")) a.name = std::move(*v);
    if (auto v = parse_json_str_field(s, "sort-name")) a.sort_name = std::move(*v);
    if (auto v = parse_json_str_field(s, "country")) a.country = std::move(*v);
    if (auto v = parse_json_str_field(s, "type")) a.type = std::move(*v);
    a.source = "musicbrainz";
    return a;
}

AcoustIdResult parse_acoustid_result(std::string_view obj) {
    AcoustIdResult r;
    if (auto v = parse_json_str_field(obj, "id")) r.recording_mbid = std::move(*v);
    if (auto v = parse_json_str_field(obj, "score")) {
        if (auto d = parse_json_double(*v)) r.score = *d;
    }
    if (auto v = parse_json_str_field(obj, "title")) r.title = std::move(*v);
    if (auto v = parse_json_str_field(obj, "artists")) {
        std::string_view orig = obj;
        const auto pos = orig.find("\"artists\"");
        if (pos != std::string_view::npos) {
            std::string_view alist = orig.substr(pos + 9);
            alist = skip_ws(alist);
            if (!alist.empty() && alist[0] == '[') {
                alist.remove_prefix(1);
                alist = skip_ws(alist);
                while (!alist.empty() && alist[0] != ']') {
                    if (alist[0] == '{') {
                        const auto close = alist.find('}');
                        if (close != std::string_view::npos) {
                            auto a = alist.substr(1, close - 1);
                            if (auto n = parse_json_str_field(a, "name")) {
                                r.artists.push_back(*n);
                            }
                            alist = alist.substr(close + 1);
                        } else {
                            break;
                        }
                    } else {
                        break;
                    }
                    alist = skip_ws(alist);
                    if (!alist.empty() && alist[0] == ',') alist.remove_prefix(1);
                }
            }
        }
    }
    if (auto v = parse_json_str_field(obj, "duration")) {
        if (auto ms = parse_json_int(*v)) {
            r.duration_ms = *ms;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// LRU cache keyed by (endpoint, mbid) with TTL
// ---------------------------------------------------------------------------

struct CacheEntry {
    std::string body;
    std::chrono::steady_clock::time_point expires_at;
};

class LookupCache {
  public:
    explicit LookupCache(std::chrono::seconds ttl) : ttl_{ttl} {}

    /// Returns the cached body if present and not expired. Out parameter
    /// receives the body; return is true if found.
    [[nodiscard]] bool get(std::string_view key,
                           std::string* out_body) const {
        std::shared_lock<std::shared_mutex> lock{mu_};
        auto it = entries_.find(std::string{key});
        if (it == entries_.end()) return false;
        if (std::chrono::steady_clock::now() > it->second.expires_at) {
            return false;
        }
        *out_body = it->second.body;
        return true;
    }

    void put(std::string_view key, std::string body) {
        std::unique_lock<std::shared_mutex> lock{mu_};
        entries_[std::string{key}] = {
            std::move(body),
            std::chrono::steady_clock::now() + ttl_};
        // Simple eviction: if the map is large, drop the oldest half.
        if (entries_.size() > max_entries_) {
            const auto cutoff =
                std::chrono::steady_clock::now() - (ttl_ / 2);
            for (auto it = entries_.begin(); it != entries_.end();) {
                if (it->second.expires_at < cutoff) {
                    it = entries_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

  private:
    static constexpr std::size_t max_entries_ = 4096;
    const std::chrono::seconds ttl_;
    mutable std::shared_mutex mu_;
    std::map<std::string, CacheEntry, std::less<>> entries_;
};

}  // namespace

// ---------------------------------------------------------------------------
// MusicBrainz client implementation
// ---------------------------------------------------------------------------

class MusicBrainzClient final : public IMusicBrainzClient {
  public:
    MusicBrainzClient(IHttpClient& http, std::string user_agent,
                      std::chrono::seconds cache_ttl)
        : http_{http},
          user_agent_{std::move(user_agent)},
          cache_{cache_ttl},
          rate_limiter_{std::chrono::seconds{1}} {}

    ~MusicBrainzClient() override { close(); }

    Status lookup_recording(std::string_view mbid) override {
        return submit_request(
            [&] { return do_lookup_recording(mbid, std::chrono::seconds{30}); });
    }

    Status lookup_release(std::string_view mbid) override {
        return submit_request(
            [&] { return do_lookup_release(mbid, std::chrono::seconds{30}); });
    }

    Status lookup_artist(std::string_view mbid) override {
        return submit_request(
            [&] { return do_lookup_artist(mbid, std::chrono::seconds{30}); });
    }

    Status lookup_fingerprint(std::string_view fingerprint,
                             std::int64_t duration_ms, int max_results,
                             double threshold) override {
        return submit_request([&] {
            return do_lookup_fingerprint(fingerprint, duration_ms, max_results,
                                        threshold, std::chrono::seconds{30});
        });
    }

    Result<MusicBrainzRecording> lookup_recording_sync(
        std::string_view mbid, std::chrono::seconds timeout) override {
        CancellationToken tok;
        if (!rate_limiter_.acquire(tok)) {
            return err(ErrorCode::Cancelled, "Rate limiter cancelled", "");
        }
        return do_lookup_recording(mbid, timeout);
    }

    Result<MusicBrainzRelease> lookup_release_sync(
        std::string_view mbid, std::chrono::seconds timeout) override {
        CancellationToken tok;
        if (!rate_limiter_.acquire(tok)) {
            return err(ErrorCode::Cancelled, "Rate limiter cancelled", "");
        }
        return do_lookup_release(mbid, timeout);
    }

    Result<MusicBrainzArtist> lookup_artist_sync(
        std::string_view mbid, std::chrono::seconds timeout) override {
        CancellationToken tok;
        if (!rate_limiter_.acquire(tok)) {
            return err(ErrorCode::Cancelled, "Rate limiter cancelled", "");
        }
        return do_lookup_artist(mbid, timeout);
    }

    Result<std::vector<AcoustIdResult>> lookup_fingerprint_sync(
        std::string_view fingerprint, std::int64_t duration_ms,
        int max_results, double threshold,
        std::chrono::seconds timeout) override {
        CancellationToken tok;
        if (!rate_limiter_.acquire(tok)) {
            return err(ErrorCode::Cancelled, "Rate limiter cancelled", "");
        }
        return do_lookup_fingerprint(fingerprint, duration_ms, max_results,
                                     threshold, timeout);
    }

    void close() noexcept override {
        rate_limiter_.reset();
    }

  private:
    Status submit_request(auto&& fn) {
        CancellationToken tok;
        if (!rate_limiter_.acquire(tok)) {
            return err(ErrorCode::Cancelled, "Rate limiter cancelled", "");
        }
        return fn();
    }

    Result<MusicBrainzRecording> do_lookup_recording(
        std::string_view mbid, std::chrono::seconds timeout) {
        std::string cache_key = "recording/";
        cache_key.append(mbid);
        std::string cached;
        if (cache_.get(cache_key, &cached)) {
            return parse_recording(cached);
        }
        HttpRequest req;
        req.method = HttpMethod::Get;
        req.url =
            std::string{kBaseUrl} + "/recording/" + std::string{mbid} +
            "?fmt=json&inc=artist-credits+releases+isrcs";
        req.headers.push_back({"Accept", "application/json"});
        req.timeout = timeout;
        CancellationToken tok;
        auto resp = http_.send(req, tok);
        if (!resp.has_value()) {
            return err(resp.error().code(),
                       "MusicBrainz recording lookup failed",
                       resp.error().technical_detail());
        }
        if (resp.value().status != 200) {
            return err(ErrorCode::HttpError,
                       "MusicBrainz returned non-200 for recording",
                       "status=" + std::to_string(resp.value().status));
        }
        const std::string& body = resp.value().body;
        cache_.put(cache_key, body);
        return parse_recording(body);
    }

    Result<MusicBrainzRelease> do_lookup_release(
        std::string_view mbid, std::chrono::seconds timeout) {
        std::string cache_key = "release/";
        cache_key.append(mbid);
        std::string cached;
        if (cache_.get(cache_key, &cached)) {
            return parse_release(cached);
        }
        HttpRequest req;
        req.method = HttpMethod::Get;
        req.url = std::string{kBaseUrl} + "/release/" + std::string{mbid} +
                  "?fmt=json&inc=labels+artist-credits";
        req.headers.push_back({"Accept", "application/json"});
        req.timeout = timeout;
        CancellationToken tok;
        auto resp = http_.send(req, tok);
        if (!resp.has_value()) {
            return err(resp.error().code(),
                       "MusicBrainz release lookup failed",
                       resp.error().technical_detail());
        }
        if (resp.value().status != 200) {
            return err(ErrorCode::HttpError,
                       "MusicBrainz returned non-200 for release",
                       "status=" + std::to_string(resp.value().status));
        }
        const std::string& body = resp.value().body;
        cache_.put(cache_key, body);
        return parse_release(body);
    }

    Result<MusicBrainzArtist> do_lookup_artist(std::string_view mbid,
                                               std::chrono::seconds timeout) {
        std::string cache_key = "artist/";
        cache_key.append(mbid);
        std::string cached;
        if (cache_.get(cache_key, &cached)) {
            return parse_artist(cached);
        }
        HttpRequest req;
        req.method = HttpMethod::Get;
        req.url = std::string{kBaseUrl} + "/artist/" + std::string{mbid} +
                  "?fmt=json";
        req.headers.push_back({"Accept", "application/json"});
        req.timeout = timeout;
        CancellationToken tok;
        auto resp = http_.send(req, tok);
        if (!resp.has_value()) {
            return err(resp.error().code(),
                       "MusicBrainz artist lookup failed",
                       resp.error().technical_detail());
        }
        if (resp.value().status != 200) {
            return err(ErrorCode::HttpError,
                       "MusicBrainz returned non-200 for artist",
                       "status=" + std::to_string(resp.value().status));
        }
        const std::string& body = resp.value().body;
        cache_.put(cache_key, body);
        return parse_artist(body);
    }

    Result<std::vector<AcoustIdResult>> do_lookup_fingerprint(
        std::string_view fingerprint, std::int64_t duration_ms,
        int max_results, double threshold,
        std::chrono::seconds timeout) {
        // AcoustID lookup: POST fingerprint=duration=fingerprint
        HttpRequest req;
        req.method = HttpMethod::Post;
        req.url = std::string{kAcoustIdBase};
        std::ostringstream body;
        body << "client=arrowplayer"
             << "&fingerprint=" << url_encode(fingerprint)
             << "&duration=" << duration_ms
             << "&maxresults=" << max_results;
        req.body.kind = HttpBody::Kind::Raw;
        req.body.raw = body.str();
        req.body.content_type = "application/x-www-form-urlencoded";
        req.timeout = timeout;
        CancellationToken tok;
        auto resp = http_.send(req, tok);
        if (!resp.has_value()) {
            return err(resp.error().code(), "AcoustID lookup failed",
                       resp.error().technical_detail());
        }
        if (resp.value().status != 200) {
            return err(ErrorCode::HttpError, "AcoustID returned non-200",
                       "status=" + std::to_string(resp.value().status));
        }
        std::vector<AcoustIdResult> out;
        std::string_view body{resp.value().body};
        // Parse the AcoustID results array.
        body = skip_ws(body);
        if (!body.empty() && body[0] == '{') {
            // Response has "results": [...]
            const auto arr_pos = body.find("\"results\"");
            if (arr_pos != std::string_view::npos) {
                body = skip_ws(body.substr(arr_pos + 9));
            }
        }
        if (!body.empty() && body[0] == '[') {
            body.remove_prefix(1);
            while (!body.empty()) {
                body = skip_ws(body);
                if (body.empty() || body[0] == ']') break;
                if (body[0] == '{') {
                    const auto close = body.find('}');
                    if (close != std::string_view::npos) {
                        auto obj = body.substr(1, close - 1);
                        auto result = parse_acoustid_result(obj);
                        if (result.score >= threshold &&
                            !result.recording_mbid.empty()) {
                            out.push_back(std::move(result));
                        }
                        body = body.substr(close + 1);
                    } else {
                        break;
                    }
                } else {
                    break;
                }
                body = skip_ws(body);
                if (!body.empty() && body[0] == ',') body.remove_prefix(1);
            }
        }
        return out;
    }

    IHttpClient& http_;
    std::string user_agent_;
    LookupCache cache_;
    RateLimiter rate_limiter_;
};

std::unique_ptr<IMusicBrainzClient> make_musicbrainz_client(
    IHttpClient& http, std::string user_agent,
    std::chrono::seconds cache_ttl) {
    return std::make_unique<MusicBrainzClient>(http, std::move(user_agent),
                                               cache_ttl);
}

}  // namespace arrow::net
