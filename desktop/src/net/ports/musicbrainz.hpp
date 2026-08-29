// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// MusicBrainz integration port — spec §17.3, REQ-NET-030.
//
// Provides lookups by recording MBID, release MBID, artist MBID, and
// Chromaprint fingerprint (AcoustID). All requests go through a shared
// rate limiter (1 req/s) and a TTL cache so repeated lookups of the same
// MBID are answered without touching the network.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "net/ports/http_port.hpp"

namespace arrow::net {

/// Metadata for a MusicBrainz recording (track).
struct MusicBrainzRecording final {
    std::string mbid;                    ///< Recording MBID (UUID)
    std::string title;
    std::vector<std::string> artists;    ///< Artist names (primary first)
    std::vector<std::string> artist_mbids;  ///< Artist MBIDs, aligned with artists
    std::optional<std::string> release_mbid;  ///< Preferred release MBID
    std::optional<std::string> release_title;
    std::optional<std::string> album_artist;
    std::optional<std::int64_t> duration_ms;  ///< Track duration in ms
    std::optional<int> track_number;
    std::optional<int> disc_number;
    std::optional<std::string> isrc;
    std::optional<std::string> barcode;
    std::optional<std::string> label;
    std::vector<std::string> genres;
    std::vector<std::string> tags;
    std::string source;                   ///< Always "musicbrainz"
};

/// Metadata for a MusicBrainz release (album).
struct MusicBrainzRelease final {
    std::string mbid;                    ///< Release MBID (UUID)
    std::string title;
    std::vector<std::string> artists;
    std::vector<std::string> artist_mbids;
    std::optional<std::string> date;     ///< "YYYY", "YYYY-MM", or "YYYY-MM-DD"
    std::optional<std::string> country;
    std::optional<std::string> barcode;
    std::optional<std::string> label;
    std::optional<std::string> asin;
    std::vector<std::string> genres;
    std::vector<std::string> media_formats;  ///< e.g. "CD", "Digital Media"
    std::string source;                   ///< Always "musicbrainz"
};

/// Metadata for a MusicBrainz artist.
struct MusicBrainzArtist final {
    std::string mbid;                    ///< Artist MBID (UUID)
    std::string name;
    std::optional<std::string> sort_name;
    std::optional<std::string> country;  ///< ISO 3166-1 alpha-2
    std::optional<std::string> type;     ///< "Person", "Group", "Orchestra", etc.
    std::vector<std::string> genres;
    std::vector<std::string> tags;
    std::string source;                  ///< Always "musicbrainz"
};

/// AcoustID fingerprint lookup result.
struct AcoustIdResult final {
    std::string recording_mbid;
    double score;                       ///< 0.0 – 1.0 confidence
    std::string title;
    std::vector<std::string> artists;
    std::optional<std::int64_t> duration_ms;
};

/// Callbacks for async lookups.
struct MusicBrainzCallbacks final {
    std::function<void(const MusicBrainzRecording&)> on_recording;
    std::function<void(const MusicBrainzRelease&)> on_release;
    std::function<void(const MusicBrainzArtist&)> on_artist;
    std::function<void(const std::vector<AcoustIdResult>&)> on_fingerprint;
    std::function<void(const Error&)> on_error;
};

/// MusicBrainz API client. Thread-compatible: a single instance may be called
/// from many threads, but calls on the same object are serialised by the
/// internal rate-limiter mutex.
class IMusicBrainzClient {
  public:
    virtual ~IMusicBrainzClient() = default;

    /// Look up a recording by its MusicBrainz recording MBID.
    /// Returns immediately; result is delivered via the on_recording callback.
    /// Cache TTL: 24 hours by default.
    [[nodiscard]] virtual Status lookup_recording(std::string_view mbid) = 0;

    /// Look up a release by its MusicBrainz release MBID.
    [[nodiscard]] virtual Status lookup_release(std::string_view mbid) = 0;

    /// Look up an artist by their MusicBrainz artist MBID.
    [[nodiscard]] virtual Status lookup_artist(std::string_view mbid) = 0;

    /// Look up a recording by Chromaprint fingerprint (AcoustID).
    /// The server returns up to `max_results` candidates; we deliver them all
    /// via the on_fingerprint callback. Threshold filters results below the
    /// given score.
    [[nodiscard]] virtual Status lookup_fingerprint(
        std::string_view fingerprint,
        std::int64_t duration_ms,
        int max_results = 5,
        double threshold = 0.5) = 0;

    /// Synchronous form. Blocks until the lookup completes or fails.
    /// Suitable for one-shot calls on a worker thread.
    [[nodiscard]] virtual Result<MusicBrainzRecording>
    lookup_recording_sync(std::string_view mbid,
                          std::chrono::seconds timeout = std::chrono::seconds{30}) = 0;

    [[nodiscard]] virtual Result<MusicBrainzRelease>
    lookup_release_sync(std::string_view mbid,
                       std::chrono::seconds timeout = std::chrono::seconds{30}) = 0;

    [[nodiscard]] virtual Result<MusicBrainzArtist>
    lookup_artist_sync(std::string_view mbid,
                       std::chrono::seconds timeout = std::chrono::seconds{30}) = 0;

    [[nodiscard]] virtual Result<std::vector<AcoustIdResult>>
    lookup_fingerprint_sync(std::string_view fingerprint,
                           std::int64_t duration_ms,
                           int max_results = 5,
                           double threshold = 0.5,
                           std::chrono::seconds timeout = std::chrono::seconds{30}) = 0;

    /// Discard the in-memory cache and close all pending requests.
    virtual void close() noexcept = 0;
};

/// Factory. `http` is the shared IHttpClient. `user_agent` is sent as the
/// User-Agent header per MusicBrainz's requirement: it must contain a valid
/// contact URL. Cache TTL defaults to 24 hours.
[[nodiscard]] std::unique_ptr<IMusicBrainzClient> make_musicbrainz_client(
    IHttpClient& http,
    std::string user_agent,
    std::chrono::seconds cache_ttl = std::chrono::hours{24});

}  // namespace arrow::net
