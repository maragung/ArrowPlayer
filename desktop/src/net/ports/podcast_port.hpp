// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Podcast port — spec §17.2, REQ-NET-020 .. REQ-NET-022.
//
// The port wraps a feed parser, an episode queue, and a download worker.
// The episode state machine (REQ-NET-020 .. REQ-NET-021) is
// unplayed → playing → played, with explicit transitions only.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "net/ports/http_port.hpp"
#include "net/ports/rss_feed.hpp"

namespace arrow::net {

/// Episode playback state. The transitions are unidirectional except for
/// the "marked unplayed" user action, which goes back to Unplayed.
enum class EpisodeState {
    Unplayed,
    Playing,
    Played
};

/// One episode in the queue. Wraps the parser's `PodcastEpisode` with state
/// and download tracking.
struct Episode final {
    PodcastEpisode info;
    EpisodeState state{EpisodeState::Unplayed};
    /// Position to resume from, in milliseconds from the start. REQ-NET-021
    /// names per-episode resume; we store it on the episode and let the
    /// audio engine ask for it before opening the file.
    std::chrono::milliseconds resume_position{0};
    /// Local path of the downloaded file, or std::nullopt if the episode
    /// has not been downloaded yet.
    std::optional<std::filesystem::path> local_path;
    /// True if the user opted in to automatic download. The queue respects
    /// REQ-NET-021: auto-download is unmetered-networks-only, and only
    /// episodes with this flag set are considered.
    bool auto_download{false};
    /// Set to true when the queue has decided to download this episode but
    /// the bytes have not been written to disk yet. Used to coalesce
    /// concurrent fetch requests for the same episode.
    bool download_in_flight{false};
};

/// A subscribed feed.
struct PodcastSubscription final {
    std::string feed_url;
    PodcastFeed feed;            ///< most recent parsed snapshot
    std::chrono::seconds refresh_interval{std::chrono::minutes{60}};
    /// 95% threshold for "mark as played" per REQ-NET-021. We store the
    /// raw threshold; the queue computes it against the episode duration.
    int played_threshold_pct{95};
    /// REQ-NET-021: episode retention policy. `keep_count = 0` means keep
    /// all; otherwise the queue drops the oldest Played episode beyond
    /// this many entries.
    std::size_t keep_count{0};
    std::vector<Episode> episodes;
    std::optional<std::string> etag;          ///< for conditional GET
    std::optional<std::string> last_modified; ///< for If-Modified-Since
};

/// Callbacks fired by the queue.
struct PodcastCallbacks final {
    /// A new episode arrived in the feed.
    std::function<void(const Episode&)> on_episode_added;
    /// Episode state changed.
    std::function<void(const Episode&)> on_episode_state;
    /// Download progress (bytes_done, bytes_total). The total may be 0
    /// when the server did not send a Content-Length.
    std::function<void(const Episode&, std::size_t, std::size_t)> on_download_progress;
    /// Download finished.
    std::function<void(const Episode&)> on_download_finished;
};

/// The podcast queue.
class IPodcastQueue {
  public:
    virtual ~IPodcastQueue() = default;

    /// Subscribe to a feed. The first refresh happens asynchronously; the
    /// returned subscription's `feed` field is empty until that completes.
    [[nodiscard]] virtual Status subscribe(std::string_view feed_url) = 0;

    /// Force an immediate refresh. Idempotent.
    [[nodiscard]] virtual Status refresh(std::string_view feed_url) = 0;

    /// Unsubscribe and drop the local cache.
    [[nodiscard]] virtual Status unsubscribe(std::string_view feed_url) = 0;

    /// Update the playback state. Used by the audio engine to drive the
    /// state machine.
    [[nodiscard]] virtual Status set_state(std::string_view feed_url,
                                            std::string_view episode_guid,
                                            EpisodeState new_state) = 0;

    /// Update the resume position. The queue stores it; the audio engine
    /// asks for it via `resume_position` before opening the file.
    [[nodiscard]] virtual Status set_resume_position(
        std::string_view feed_url, std::string_view episode_guid,
        std::chrono::milliseconds position) = 0;

    /// Mark an episode for automatic download. The queue will pick it up
    /// on the next cycle, subject to REQ-NET-021 (unmetered networks only).
    [[nodiscard]] virtual Status request_download(std::string_view feed_url,
                                                 std::string_view episode_guid) = 0;

    /// Pause or resume all in-flight downloads.
    virtual void pause_downloads() noexcept = 0;
    virtual void resume_downloads() noexcept = 0;

    /// Snapshot of a subscription.
    [[nodiscard]] virtual std::optional<PodcastSubscription> subscription(
        std::string_view feed_url) const = 0;

    /// Wire callbacks.
    virtual void set_callbacks(PodcastCallbacks callbacks) noexcept = 0;
};

/// Factory: produce the default queue. `download_root` is the directory
/// downloaded files are written to (REQ-SET-003).
[[nodiscard]] std::unique_ptr<IPodcastQueue> make_default_podcast_queue(
    const std::filesystem::path& download_root);

}  // namespace arrow::net
