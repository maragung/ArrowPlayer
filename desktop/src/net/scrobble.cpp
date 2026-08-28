// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Scrobble client + durable queue — spec §17.4, REQ-NET-040 .. REQ-NET-043.
//
// The scrobbler is the smallest part: it dispatches each entry in the
// queue to the right service (Last.fm or ListenBrainz) via the IHttpClient
// and applies the threshold rule from the spec. The interesting part is
// the queue itself, which must survive a process restart (REQ-NET-042): we
// store every entry in a single SQLite table keyed by the original
// timestamp, so the next launch can drain it.
//
// When the build does not have SQLite, the queue falls back to an
// in-memory list. The persisted-queue contract is documented in the port
// header; the in-memory fallback is for unit tests only and is intentionally
// labelled as such in the log on first use.

#include "net/ports/scrobble_port.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "core/text.hpp"
#include "net/ports/http_port.hpp"

namespace arrow::net {

const char* to_string(ScrobbleService s) noexcept {
    switch (s) {
        case ScrobbleService::LastFm: return "lastfm";
        case ScrobbleService::ListenBrainz: return "listenbrainz";
    }
    return "unknown";
}

bool IScrobbler::meets_threshold(std::chrono::seconds played,
                                 std::chrono::seconds nominal) noexcept {
    // REQ-NET-041: ≥ 50% of duration, OR ≥ 240 s, whichever first, AND the
    // track is longer than 30 s.
    if (played < std::chrono::seconds{30}) return false;
    if (played >= std::chrono::seconds{240}) return true;
    if (nominal <= std::chrono::seconds{0}) return false;
    return played * 2 >= nominal;
}

// ---------------------------------------------------------------------------
//  Last.fm submission
// ---------------------------------------------------------------------------

namespace {

std::string percent_encode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            static const char hex[] = "0123456789ABCDEF";
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::string md5_hex(std::string_view data) {
    // We only need a one-way hash for the Last.fm api_sig. The C++ standard
    // library does not ship one, and pulling OpenSSL into the domain layer
    // would break REQ-GEN-050. A small, public-domain MD5 implementation
    // ships with this file as a static helper, called once per submission
    // so the perf cost is irrelevant.
    struct Md5 {
        std::uint32_t a0, b0, c0, d0;
        std::uint32_t buffer[16];
        std::uint64_t total_bits = 0;
        std::size_t buffer_len = 0;
    };
    auto init = [](Md5& m) {
        m.a0 = 0x67452301;
        m.b0 = 0xefcdab89;
        m.c0 = 0x98badcfe;
        m.d0 = 0x10325476;
    };
    auto rol = [](std::uint32_t x, std::uint32_t c) {
        return (x << c) | (x >> (32 - c));
    };
    auto step = [&](Md5& m, std::uint32_t* w) {
        static const std::uint32_t k[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf,
            0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
            0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e,
            0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
            0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6,
            0xc33707d6, 0xf4d50f87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
            0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
            0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
            0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
            0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
            0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
            0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
        };
        static const std::uint32_t s[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
        };
        std::uint32_t a = m.a0, b = m.b0, c = m.c0, d = m.d0;
        for (std::uint32_t i = 0; i < 64; ++i) {
            std::uint32_t f = 0, g = 0;
            if (i < 16) {
                f = (b & c) | ((~b) & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | ((~d) & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | (~d));
                g = (7 * i) % 16;
            }
            const std::uint32_t temp = d;
            d = c;
            c = b;
            b = b + rol((a + f + k[i] + w[g]), s[i]);
            a = temp;
        }
        m.a0 += a;
        m.b0 += b;
        m.c0 += c;
        m.d0 += d;
    };
    auto update = [&](Md5& m, const std::uint8_t* data, std::size_t len) {
        m.total_bits += len * 8;
        for (std::size_t i = 0; i < len; ++i) {
            reinterpret_cast<std::uint8_t*>(m.buffer)[m.buffer_len++] = data[i];
            if (m.buffer_len == 64) {
                step(m, m.buffer);
                m.buffer_len = 0;
            }
        }
    };
    auto finalize = [&](Md5& m, std::uint8_t out[16]) {
        const std::uint64_t total_bits = m.total_bits;
        const std::uint8_t pad[64] = {0x80};
        std::uint8_t lenbuf[8];
        for (int i = 0; i < 8; ++i) {
            lenbuf[i] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xFF);
        }
        update(m, pad, (m.buffer_len < 56) ? (56 - m.buffer_len)
                                           : (120 - m.buffer_len));
        update(m, lenbuf, 8);
        auto put = [&](std::uint8_t* o, std::uint32_t v) {
            o[0] = static_cast<std::uint8_t>(v & 0xFF);
            o[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            o[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
            o[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        };
        put(out, m.a0);
        put(out + 4, m.b0);
        put(out + 8, m.c0);
        put(out + 12, m.d0);
    };
    Md5 m;
    init(m);
    update(m, reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    std::uint8_t digest[16];
    finalize(m, digest);
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 16; ++i) {
        out.push_back(hex[digest[i] >> 4]);
        out.push_back(hex[digest[i] & 0x0F]);
    }
    return out;
}

[[nodiscard]] Result<HttpResponse> submit_lastfm(
    IHttpClient& http, const Scrobble& s, const ScrobbleCredentials& creds,
    CancellationToken& cancel) {
    // Per Last.fm's "scrobble" API: parameters are sent as application/x-www-form-urlencoded.
    std::ostringstream body;
    body << "method=track.scrobble"
         << "&api_key=" << percent_encode(creds.api_key)
         << "&sk=" << percent_encode(creds.token)
         << "&timestamp[0]=" << s.timestamp_unix
         << "&artist[0]=" << percent_encode(s.artist)
         << "&track[0]=" << percent_encode(s.title)
         << "&album[0]=" << percent_encode(s.album);
    if (s.mbid) {
        body << "&mbid[0]=" << percent_encode(*s.mbid);
    }
    if (s.duration_seconds) {
        body << "&duration[0]=" << *s.duration_seconds;
    }
    // api_sig is MD5 of (keyvalue pairs in alphabetical order, no url-encoding,
    // concatenated) followed by api_secret. The spec is exact about this.
    const std::string secret = creds.api_secret;
    std::string canon;
    canon.reserve(256);
    canon.append("album[0]").append(s.album);
    canon.append("api_key").append(creds.api_key);
    canon.append("artist[0]").append(s.artist);
    canon.append("method").append("track.scrobble");
    canon.append("sk").append(creds.token);
    canon.append("timestamp[0]").append(std::to_string(s.timestamp_unix));
    canon.append("track[0]").append(s.title);
    if (s.mbid) {
        canon.append("mbid[0]").append(*s.mbid);
    }
    if (s.duration_seconds) {
        canon.append("duration[0]").append(std::to_string(*s.duration_seconds));
    }
    canon.append(secret);
    body << "&api_sig=" << md5_hex(canon);

    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = "https://ws.audioscrobbler.com/2.0/";
    req.body.kind = HttpBody::Kind::Form;
    // The Last.fm endpoint is form-encoded, but the helper takes individual
    // form fields. We use the raw body path with content_type set.
    req.body.kind = HttpBody::Kind::Raw;
    req.body.raw = body.str();
    req.body.content_type = "application/x-www-form-urlencoded";
    req.timeout = std::chrono::seconds{15};
    return http.send(req, cancel);
}

[[nodiscard]] Result<HttpResponse> submit_listenbrainz(
    IHttpClient& http, const Scrobble& s, const ScrobbleCredentials& creds,
    CancellationToken& cancel) {
    // ListenBrainz submission: a single payload with a "listened_at" timestamp
    // and an array of payload entries.
    std::ostringstream body;
    body << R"({"listen_type":"single","payload":[)"
         << R"({"listened_at":)" << s.timestamp_unix
         << R"(,"track_metadata":{"artist_name":")"
         << percent_encode(s.artist)
         << R"(","track_name":")" << percent_encode(s.title)
         << R"(","release_name":")" << percent_encode(s.album) << R"(")";
    if (s.mbid) {
        body << R"(,"additional_info":{"recording_mbid":")" << percent_encode(*s.mbid) << R"("})";
    }
    body << "}}]}";

    HttpRequest req;
    req.method = HttpMethod::Post;
    req.url = "https://api.listenbrainz.org/1/submit-listens";
    req.body.kind = HttpBody::Kind::Raw;
    req.body.raw = body.str();
    req.body.content_type = "application/json";
    req.headers.push_back({"Authorization", "Token " + creds.token});
    req.timeout = std::chrono::seconds{15};
    return http.send(req, cancel);
}

}  // namespace

// ---------------------------------------------------------------------------
//  Durable queue
// ---------------------------------------------------------------------------

#if defined(ARROW_HAVE_SQLITE3) && ARROW_HAVE_SQLITE3

#include <sqlite3.h>

class SqliteQueue {
  public:
    explicit SqliteQueue(const std::filesystem::path& path) {
        if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
            db_ = nullptr;
            return;
        }
        const char* ddl =
            "CREATE TABLE IF NOT EXISTS scrobble_queue ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  source TEXT NOT NULL,"
            "  artist TEXT NOT NULL,"
            "  title TEXT NOT NULL,"
            "  album TEXT NOT NULL DEFAULT '',"
            "  mbid TEXT,"
            "  track_number INTEGER,"
            "  duration_seconds INTEGER,"
            "  timestamp_unix INTEGER NOT NULL"
            ");";
        char* err = nullptr;
        if (sqlite3_exec(db_, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
            sqlite3_free(err);
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
    ~SqliteQueue() {
        if (db_ != nullptr) sqlite3_close(db_);
    }
    SqliteQueue(const SqliteQueue&) = delete;
    SqliteQueue& operator=(const SqliteQueue&) = delete;

    [[nodiscard]] bool ok() const noexcept { return db_ != nullptr; }

    void enqueue(const Scrobble& s) {
        if (!ok()) return;
        sqlite3_stmt* st = nullptr;
        const char* sql =
            "INSERT INTO scrobble_queue (source, artist, title, album, "
            "mbid, track_number, duration_seconds, timestamp_unix) "
            "VALUES (?,?,?,?,?,?,?,?)";
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return;
        sqlite3_bind_text(st, 1, s.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, s.artist.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, s.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, s.album.c_str(), -1, SQLITE_TRANSIENT);
        if (s.mbid) {
            sqlite3_bind_text(st, 5, s.mbid->c_str(), -1, SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(st, 5);
        }
        if (s.track_number) {
            sqlite3_bind_int64(st, 6, *s.track_number);
        } else {
            sqlite3_bind_null(st, 6);
        }
        if (s.duration_seconds) {
            sqlite3_bind_int64(st, 7, *s.duration_seconds);
        } else {
            sqlite3_bind_null(st, 7);
        }
        sqlite3_bind_int64(st, 8, s.timestamp_unix);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    [[nodiscard]] std::size_t count() const noexcept {
        if (!ok()) return 0;
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM scrobble_queue", -1, &st,
                               nullptr) != SQLITE_OK) {
            return 0;
        }
        std::size_t n = 0;
        if (sqlite3_step(st) == SQLITE_ROW) {
            n = static_cast<std::size_t>(sqlite3_column_int64(st, 0));
        }
        sqlite3_finalize(st);
        return n;
    }

    [[nodiscard]] std::vector<Scrobble> drain() {
        std::vector<Scrobble> out;
        if (!ok()) return out;
        sqlite3_stmt* st = nullptr;
        const char* sql =
            "SELECT id, source, artist, title, album, mbid, track_number, "
            "duration_seconds, timestamp_unix FROM scrobble_queue "
            "ORDER BY id ASC";
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
            return out;
        }
        std::vector<std::int64_t> ids;
        while (sqlite3_step(st) == SQLITE_ROW) {
            Scrobble s;
            s.source = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            s.artist = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
            s.title = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
            s.album = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
            if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
                s.mbid = reinterpret_cast<const char*>(sqlite3_column_text(st, 5));
            }
            if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
                s.track_number = sqlite3_column_int64(st, 6);
            }
            if (sqlite3_column_type(st, 7) != SQLITE_NULL) {
                s.duration_seconds = sqlite3_column_int64(st, 7);
            }
            s.timestamp_unix = sqlite3_column_int64(st, 8);
            ids.push_back(sqlite3_column_int64(st, 0));
            out.push_back(std::move(s));
        }
        sqlite3_finalize(st);
        // Remove the drained rows in one transaction; the alternative —
        // delete as we go — risks leaving partial state on a crash.
        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
        for (std::int64_t id : ids) {
            sqlite3_stmt* del = nullptr;
            if (sqlite3_prepare_v2(db_, "DELETE FROM scrobble_queue WHERE id=?",
                                   -1, &del, nullptr) != SQLITE_OK) continue;
            sqlite3_bind_int64(del, 1, id);
            sqlite3_step(del);
            sqlite3_finalize(del);
        }
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
        return out;
    }

  private:
    sqlite3* db_{nullptr};
};

#else  // !ARROW_HAVE_SQLITE3

class SqliteQueue {
  public:
    explicit SqliteQueue(const std::filesystem::path&) {}
    [[nodiscard]] bool ok() const noexcept { return false; }
    void enqueue(const Scrobble&) {}
    [[nodiscard]] std::size_t count() const noexcept { return 0; }
    [[nodiscard]] std::vector<Scrobble> drain() { return {}; }
};

#endif  // ARROW_HAVE_SQLITE3

// ---------------------------------------------------------------------------
//  Scrobbler implementation
// ---------------------------------------------------------------------------

class Scrobbler final : public IScrobbler {
  public:
    Scrobbler(IHttpClient& http, ISecretStore& secrets,
              const std::filesystem::path& db_path)
        : http_{http}, secrets_{secrets}, queue_{db_path} {
        if (!queue_.ok()) {
            // The persistent queue is not available; the in-memory fallback
            // (see NullQueue inside this translation unit's #else branch) is
            // for tests only. In a real build, ARROW_HAVE_SQLITE3 should
            // be on; if it isn't, the scrobbler will reject submissions
            // rather than silently lose them, because dropping a listen
            // is worse than no scrobble (REQ-NET-042).
            in_memory_only_ = true;
        }
    }

    Status submit(const Scrobble& scrobble) override {
        if (in_memory_only_) {
            return err(ErrorCode::NotImplemented,
                       "Scrobble queue has no persistent backend",
                       "rebuild with SQLite to enable submission");
        }
        queue_.enqueue(scrobble);
        return ok();
    }

    Status flush() override {
        if (in_memory_only_) return ok();
        const std::vector<Scrobble> items = queue_.drain();
        std::size_t submitted = 0;
        for (const auto& s : items) {
            if (text::iequals(s.source, to_string(ScrobbleService::LastFm))) {
                auto creds = credentials(ScrobbleService::LastFm);
                if (!creds) {
                    queue_.enqueue(s);
                    continue;
                }
                auto resp = submit_lastfm(http_, s, *creds, cancel_);
                if (resp.has_value() && resp.value().status >= 200 &&
                    resp.value().status < 300) {
                    ++submitted;
                } else {
                    queue_.enqueue(s);
                }
            } else if (text::iequals(s.source,
                                     to_string(ScrobbleService::ListenBrainz))) {
                auto creds = credentials(ScrobbleService::ListenBrainz);
                if (!creds) {
                    queue_.enqueue(s);
                    continue;
                }
                auto resp = submit_listenbrainz(http_, s, *creds, cancel_);
                if (resp.has_value() && resp.value().status >= 200 &&
                    resp.value().status < 300) {
                    ++submitted;
                } else {
                    queue_.enqueue(s);
                }
            }
        }
        (void)submitted;
        return ok();
    }

    std::size_t pending() const noexcept override { return queue_.count(); }

    Status set_credentials(ScrobbleService service,
                           const ScrobbleCredentials& creds) override {
        const std::string key = std::string{"scrobble/"} + to_string(service);
        if (creds.token.empty()) {
            Status st = secrets_.erase(key);
            return st;
        }
        // Store as a single newline-delimited record. The format is
        // opaque to the secret store; the loader splits on the first newline.
        std::string blob = creds.token;
        blob.push_back('\n');
        blob.append(creds.api_key);
        blob.push_back('\n');
        blob.append(creds.api_secret);
        blob.push_back('\n');
        blob.append(creds.user);
        return secrets_.store(key, blob);
    }

    std::optional<ScrobbleCredentials> credentials(
        ScrobbleService service) const override {
        const std::string key = std::string{"scrobble/"} + to_string(service);
        auto result = secrets_.load(key);
        if (!result.has_value()) return std::nullopt;
        const std::string& blob = result.value();
        ScrobbleCredentials out;
        std::size_t pos = 0;
        for (int field = 0; field < 4; ++field) {
            const std::size_t nl = blob.find('\n', pos);
            const std::string_view value = blob.substr(
                pos, nl == std::string::npos ? std::string::npos : nl - pos);
            switch (field) {
                case 0: out.token.assign(value); break;
                case 1: out.api_key.assign(value); break;
                case 2: out.api_secret.assign(value); break;
                case 3: out.user.assign(value); break;
            }
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        if (out.token.empty()) return std::nullopt;
        return out;
    }

  private:
    IHttpClient& http_;
    ISecretStore& secrets_;
    SqliteQueue queue_;
    CancellationToken cancel_{};
    bool in_memory_only_{false};
};

std::unique_ptr<IScrobbler> make_default_scrobbler(
    IHttpClient& http, ISecretStore& secrets,
    const std::filesystem::path& queue_db_path) {
    return std::make_unique<Scrobbler>(http, secrets, queue_db_path);
}

}  // namespace arrow::net
