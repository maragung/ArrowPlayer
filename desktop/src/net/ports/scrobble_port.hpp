// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Scrobble port — spec §17.4, REQ-NET-040 .. REQ-NET-043.
//
// One port, two implementations (Last.fm, ListenBrainz), one durable queue.
// The scrobble threshold is fixed by the spec at 50% of duration or 240 s,
// whichever is first; the port applies the rule and never hands an
// under-threshold play to the queue.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "net/ports/http_port.hpp"

namespace arrow::net {

/// A scrobble as the queue stores it. Mirrors the Last.fm "now playing"
/// and "scrobble" submission shapes; the ListenBrainz submission uses the
/// same fields under a different endpoint.
struct Scrobble final {
    std::string artist;
    std::string title;
    std::string album;
    std::optional<std::string> mbid;            ///< MusicBrainz recording id
    std::optional<std::int64_t> track_number;
    std::optional<std::int64_t> duration_seconds;  ///< as reported by the source
    /// When the play was observed, in seconds since the Unix epoch. The
    /// queue preserves this so the original timestamp survives restart
    /// (REQ-NET-042).
    std::int64_t timestamp_unix{0};
    /// The provider the play was sourced through. Helps when a user has
    /// both Last.fm and ListenBrainz enabled; the queue dispatches each
    /// play to whichever services the user authenticated against.
    std::string source;
};

/// Credentials. Stored in the OS secret store (REQ-NET-043). The plain-text
/// form is never written to a settings file; this struct is only held in
/// memory while the scrobbler is running.
struct ScrobbleCredentials final {
    /// Last.fm: session key, returned by the auth handshake. ListenBrainz:
    /// user token (a long hex string the user copies from their profile).
    std::string token;
    /// Last.fm: API key. ListenBrainz: empty.
    std::string api_key;
    /// Last.fm: API secret. ListenBrainz: empty.
    std::string api_secret;
    /// User name, kept for the Last.fm "now playing" handshake only.
    std::string user;
};

/// Submission target.
enum class ScrobbleService { LastFm, ListenBrainz };

[[nodiscard]] const char* to_string(ScrobbleService s) noexcept;

/// The secret store contract. REQ-NET-043 names the OS secret store, but
/// the headless / CI build has none, so the port allows a no-op
/// implementation. The real SecretService / Credential Manager / Keystore
/// adapters live in separate translation units, behind this port.
class ISecretStore {
  public:
    virtual ~ISecretStore() = default;
    [[nodiscard]] virtual Status store(std::string_view key,
                                       std::string_view secret) = 0;
    [[nodiscard]] virtual Result<std::string> load(std::string_view key) = 0;
    [[nodiscard]] virtual Status erase(std::string_view key) = 0;
};

/// The scrobbler port. The host wires an implementation per service; both
/// share the same IHttpClient and the same durable queue.
class IScrobbler {
  public:
    virtual ~IScrobbler() = default;

    /// Apply the 50%-or-240 s rule. The host calls this from the playback
    /// graph when a track ends, and submits the play only when the
    /// function returns true. The check accounts for the play duration
    /// measured by the audio engine, not the track's nominal length.
    [[nodiscard]] static bool meets_threshold(
        std::chrono::seconds played,
        std::chrono::seconds nominal) noexcept;

    /// Enqueue a scrobble for submission. The queue persists it before the
    /// function returns, so a crash immediately after this call still
    /// counts (REQ-NET-042).
    [[nodiscard]] virtual Status submit(const Scrobble& scrobble) = 0;

    /// Drain the queue once. Idempotent; safe to call on a timer.
    [[nodiscard]] virtual Status flush() = 0;

    /// Current queue size. Cheap.
    [[nodiscard]] virtual std::size_t pending() const noexcept = 0;

    /// Set credentials. Empty `creds.token` triggers "log out" and erases
    /// the secret-store entry (REQ-NET-043, "log out deletes them").
    [[nodiscard]] virtual Status set_credentials(ScrobbleService service,
                                                 const ScrobbleCredentials& creds) = 0;

    /// Look up current credentials. Returns std::nullopt if not set.
    [[nodiscard]] virtual std::optional<ScrobbleCredentials> credentials(
        ScrobbleService service) const = 0;
};

/// Factory.
[[nodiscard]] std::unique_ptr<IScrobbler> make_default_scrobbler(
    IHttpClient& http, ISecretStore& secrets,
    const std::filesystem::path& queue_db_path);

/// A no-op secret store. Used on headless builds and in tests where the OS
/// secret store is not available (REQ-NET-043, "fall back to a no-op").
class NullSecretStore final : public ISecretStore {
  public:
    Status store(std::string_view, std::string_view) override { return ok(); }
    Result<std::string> load(std::string_view) override {
        return err(ErrorCode::FileNotFound, "No secret store available", "");
    }
    Status erase(std::string_view) override { return ok(); }
};

}  // namespace arrow::net
