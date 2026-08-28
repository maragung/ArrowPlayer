// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Internet radio port — spec §17.1, REQ-NET-010 .. REQ-NET-015.
//
// The radio client wraps three things: an IHttpClient (for the HTTP control
// plane — fetching the .m3u8, .pls, .m3u, ICY handshake), an ICY metadata
// parser (the inline StreamTitle= blocks), and an HLS playlist parser. The
// port itself is the playback surface: a downstream consumer (the audio
// engine) pulls bytes from `read()` while the radio client demuxes ICY
// metadata out of the stream and surfaces track changes as events.

#pragma once

#include <array>
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

/// Station description.
struct RadioStation final {
    std::string name;
    std::string url;             ///< raw URL the user typed
    std::string genre;           ///< ICY genre, if advertised
    std::string home_page;       ///< ICY-url, if advertised
    int bitrate_kbps{0};         ///< icy-br
    bool is_https{false};        ///< true for https:// streams
};

/// One track as the radio client reports it. A "track" here is whatever the
/// stream is currently playing — for raw MP3/AAC streams it is the ICY
/// StreamTitle; for HLS streams it is the segment name.
struct RadioTrack final {
    std::string artist;
    std::string title;
    std::string station_name;
    std::string artwork_url;     ///< empty unless the ICY payload carries one
};

/// State of the radio connection. The state machine mirrors the spec's
/// reconnect ladder (§17.1.4 / REQ-NET-013): Connecting → Buffering → Playing
/// → (Disconnecting → Reconnecting) → Connecting.
enum class RadioState {
    Idle,
    Connecting,
    Buffering,
    Playing,
    Paused,
    Reconnecting,
    Failed
};

struct RadioCallbacks final {
    /// Fired on every state transition.
    std::function<void(RadioState)> on_state;
    /// Fired on every metadata change. The new track replaces the old one
    /// in the OS media controls (REQ-NET-011).
    std::function<void(const RadioTrack&)> on_track;
    /// Fired with the raw stream bytes the audio engine should consume.
    /// The radio client guarantees that the buffer is filled in order and
    /// never reuses it.
    std::function<void(const std::uint8_t* data, std::size_t bytes)> on_audio;
    /// Fired on errors. The state will already have moved to Reconnecting
    /// or Failed; this is informational.
    std::function<void(const Error&)> on_error;
};

/// Exponent for the backoff ladder (REQ-NET-013): 1, 2, 5, 10, 30 seconds.
/// The radio client uses the index into this list to decide when the next
/// retry fires; beyond 30 s, the same 30 s repeats forever (the spec does
/// not name an upper bound, so we cap at "every 30 s" rather than inventing
/// a new policy).
inline constexpr std::array<std::chrono::seconds, 5> kReconnectBackoff = {
    std::chrono::seconds{1},
    std::chrono::seconds{2},
    std::chrono::seconds{5},
    std::chrono::seconds{10},
    std::chrono::seconds{30},
};

/// The radio client. One per active stream.
class IRadioClient {
  public:
    virtual ~IRadioClient() = default;

    /// Begin streaming. Returns once the connect handshake is in flight; the
    /// actual audio arrives via the `on_audio` callback.
    [[nodiscard]] virtual Status open(const RadioStation& station,
                                      IHttpClient& http,
                                      const RadioCallbacks& callbacks) = 0;

    /// Stop streaming immediately. Idempotent.
    virtual void close() noexcept = 0;

    /// Pause audio callback delivery (state moves to Paused); re-enable
    /// with `resume()`. The underlying HTTP connection is kept open.
    virtual void pause() noexcept = 0;

    /// Counterpart to `pause()`. Has no effect unless the client is paused.
    virtual void resume() noexcept = 0;

    /// Snapshot of the current state. Cheap; intended for the UI to poll.
    [[nodiscard]] virtual RadioState state() const noexcept = 0;

    /// Current track, if known. Empty until the first ICY block arrives.
    [[nodiscard]] virtual std::optional<RadioTrack> current_track() const noexcept = 0;
};

/// Factory: produce the default client.
[[nodiscard]] std::unique_ptr<IRadioClient> make_default_radio_client();

/// Parse a station playlist (.pls, .m3u) and return the contained streams.
/// We do not auto-follow: the radio client only opens the first entry.
[[nodiscard]] Result<std::vector<RadioStation>> parse_station_playlist(
    std::string_view text, std::string_view content_type);

}  // namespace arrow::net
