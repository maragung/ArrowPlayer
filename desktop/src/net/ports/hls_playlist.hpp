// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// HLS playlist parser — spec §17.1, REQ-NET-010 / REQ-NET-011.
//
// HLS is RFC 8216: a master playlist references one or more variant streams
// (each with a bandwidth), and a media playlist lists byte ranges for each
// segment. Arrow's radio client only needs the segment list of a single
// variant, so the parser is deliberately small: it does not implement
// encryption (EXT-X-KEY) or byte-range segments, and returns a structured
// error for those rather than silently truncating.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace arrow::net {

/// One variant in a master playlist.
struct HlsVariant final {
    std::int64_t bandwidth_bps{0};            ///< BANDWIDTH attribute
    std::int64_t average_bandwidth_bps{0};    ///< AVERAGE-BANDWIDTH if present
    std::string uri;                          ///< URL of the media playlist
    std::string codecs;                       ///< CODECS, e.g. "mp4a.40.2"
    std::string resolution;                   ///< RESOLUTION, e.g. "1280x720"
    std::optional<double> frame_rate;         ///< FRAME-RATE
};

/// One segment in a media playlist.
struct HlsSegment final {
    std::string uri;          ///< resolved against the playlist URL
    std::chrono::seconds duration{0};
    std::optional<std::string> title;     ///< EXTINF title, if any
    /// Sequence number from #EXT-X-MEDIA-SEQUENCE. Set on the first segment
    /// and inherited by the parser for each subsequent entry, so the radio
    /// client can detect when the server rotated the list.
    std::int64_t sequence{0};
};

/// A media playlist. The segments are ready to be fetched in order.
struct HlsMediaPlaylist final {
    std::int64_t media_sequence{0};
    std::int64_t target_duration{0};
    bool endlist{false};
    std::vector<HlsSegment> segments;
    /// Version tag from #EXT-X-VERSION, if present. Some servers use the
    /// version to gate floating-point duration syntax (v2+ allows non-integer
    /// EXTINF; v3+ allows EXT-X-BYTERANGE); we record it so the radio
    /// client can downgrade gracefully.
    std::optional<int> version;
};

/// A master playlist (only variants, no segments).
struct HlsMasterPlaylist final {
    std::vector<HlsVariant> variants;
    std::optional<int> version;
};

/// Parse a master playlist (the #EXTM3U header plus at least one
/// #EXT-X-STREAM-INF line).
[[nodiscard]] Result<HlsMasterPlaylist> parse_hls_master(std::string_view text,
                                                        std::string_view base_url);

/// Parse a media playlist (#EXTM3U + segment list). The base_url is the URL
/// the playlist was fetched from, used to resolve relative segment URIs per
/// RFC 8216 §4.1.
[[nodiscard]] Result<HlsMediaPlaylist> parse_hls_media(std::string_view text,
                                                      std::string_view base_url);

/// Pick the best variant for a given downlink bandwidth. -1 means "any";
/// we pick the highest BANDWIDTH not exceeding the limit. If every variant
/// exceeds the limit, the lowest one is returned (better a low-bitrate
/// stream than nothing).
[[nodiscard]] const HlsVariant* pick_hls_variant(const HlsMasterPlaylist& master,
                                                 std::int64_t max_bandwidth_bps) noexcept;

}  // namespace arrow::net
