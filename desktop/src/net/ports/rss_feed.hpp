// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// RSS 2.0 / Atom / iTunes podcast extensions parser — spec §17.2,
// REQ-NET-020 .. REQ-NET-022.
//
// Every feed document Arrow parses is untrusted (§21.2, REQ-SEC-002). The
// parser is hard-capped at 8 MiB and 64 levels of nesting (REQ-NET-022), and
// refuses anything that smells like an XXE payload (no external entity
// resolution — the only entity reference we ever honour is the built-in XML
// predefined set: &amp; &lt; &gt; &apos; &quot;). Any other entity is an
// error, because the alternative is billion-laughs with no flag from us.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace arrow::net {

/// One podcast enclosure (the actual MP3 / M4A / etc file).
struct PodcastEnclosure final {
    std::string url;            ///< http(s) URL of the audio file
    std::string mime_type;      ///< e.g. "audio/mpeg"
    std::int64_t length_bytes{0};
};

/// One podcast episode. Mirrors the iTunes extension fields the spec
/// explicitly names: GUID, enclosure, duration, publication date, season /
/// episode numbers.
struct PodcastEpisode final {
    std::string guid;                       ///< globally unique id
    std::string title;
    std::string description;                ///< plain text, HTML stripped
    std::string link;                       ///< web page for this episode
    std::optional<PodcastEnclosure> enclosure;
    std::chrono::seconds duration{0};
    /// RFC 822 / RFC 3339 pubDate, kept as the original string. The podcast
    /// queue parses it once for ordering; the raw value is preserved for
    /// export back to a feed reader.
    std::string pub_date_raw;
    /// Seconds since the Unix epoch when the episode was published. Optional
    /// because some feeds omit the date.
    std::optional<std::int64_t> pub_date_epoch;
    std::optional<int> season;
    std::optional<int> episode_number;
    /// A stable hash of (guid || enclosure_url) used by the queue to detect
    /// "the same episode appeared under a different feed". Not exposed
    /// directly to the UI.
    std::string identity_hash;
};

/// A podcast feed.
struct PodcastFeed final {
    std::string title;
    std::string author;
    std::string description;
    std::string link;                       ///< canonical web page
    std::string artwork_url;                ///< iTunes image (or RSS logo)
    std::string language;                   ///< e.g. "en-us"
    std::string copyright;
    std::string last_build_date_raw;        ///< for conditional GET
    std::optional<std::string> etag;        ///< for conditional GET
    std::vector<PodcastEpisode> episodes;
};

/// Hard limits matching REQ-NET-022 and the §21.2 parser-hardening rules.
/// 8 MiB is the §17.2 cap; the other defaults match what core/json uses.
struct FeedLimits {
    std::size_t max_bytes =
        static_cast<std::size_t>(8u) * 1024u * 1024u;  ///< 8 MiB
    std::size_t max_depth = 64;
    std::size_t max_elements = 200'000;
};

/// Parse a feed. Accepts RSS 2.0 with iTunes extensions, Atom 1.0, and
/// plain Atom 0.3; anything else is reported as ErrorCode::MalformedHeader.
[[nodiscard]] Result<PodcastFeed> parse_podcast_feed(std::string_view text,
                                                     const FeedLimits& limits = {});

}  // namespace arrow::net
