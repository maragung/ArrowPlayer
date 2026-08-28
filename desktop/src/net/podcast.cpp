// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Podcast queue — spec §17.2, REQ-NET-020 .. REQ-NET-022.
//
// The queue is a state machine over a collection of PodcastSubscription
// objects. The fetch path is one synchronous pass: a refresh pulls the
// feed body through IHttpClient, parses it (or honours 304 Not Modified),
// diffs the new episode set against what was already known, fires
// on_episode_added for every newcomer, and persists nothing yet (the
// durable backend is left as a follow-up).
//
// The download path is currently a no-op: the queue accepts the request,
// marks the episode as download_in_flight, and emits on_download_finished
// immediately. The real worker is the file-system-aware background
// downloader that REQ-NET-021 calls for; it is wired in via a separate
// translation unit when the §11.5 IDiskPort contract lands.

#include "net/ports/podcast_port.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "net/ports/http_port.hpp"
#include "net/ports/rss_feed.hpp"

namespace arrow::net {

namespace {

bool same_string(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && a.compare(b) == 0;
}

}  // namespace

class PodcastQueue final : public IPodcastQueue {
  public:
    explicit PodcastQueue(std::filesystem::path root)
        : download_root_{std::move(root)} {}

    Status subscribe(std::string_view feed_url) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (subscriptions_.find(std::string{feed_url}) != subscriptions_.end()) {
            return ok();
        }
        PodcastSubscription sub;
        sub.feed_url = std::string{feed_url};
        subscriptions_.emplace(sub.feed_url, std::move(sub));
        return ok();
    }

    Status refresh(std::string_view feed_url) override {
        PodcastSubscription copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscriptions_.find(std::string{feed_url});
            if (it == subscriptions_.end()) {
                return err(ErrorCode::InvalidArgument,
                           "Podcast subscription not found",
                           "call subscribe() first");
            }
            copy = it->second;
        }
        if (http_ == nullptr) {
            return err(ErrorCode::InvalidState,
                       "Podcast queue has no IHttpClient wired",
                       "the host must set_http() before refresh()");
        }
        HttpRequest req;
        req.method = HttpMethod::Get;
        req.url = copy.feed_url;
        req.timeout = std::chrono::seconds{15};
        if (copy.etag) {
            req.headers.push_back({"If-None-Match", *copy.etag});
        }
        if (copy.last_modified) {
            req.headers.push_back({"If-Modified-Since", *copy.last_modified});
        }
        auto result = http_->send(req, cancel_);
        if (!result.has_value()) return result.error();
        const HttpResponse& resp = result.value();
        if (resp.status == 304) {
            // Not modified — nothing to do.
            return ok();
        }
        if (resp.status < 200 || resp.status >= 300) {
            return err(ErrorCode::HttpError,
                       "Podcast feed returned a non-2xx status",
                       "status=" + std::to_string(resp.status));
        }
        auto parsed = parse_podcast_feed(resp.body);
        if (!parsed.has_value()) return parsed.error();
        // ETag / Last-Modified capture for next conditional fetch.
        for (const auto& h : resp.headers) {
            if (h.name == "ETag" || h.name == "etag") {
                copy.etag = h.value;
            } else if (h.name == "Last-Modified" || h.name == "last-modified") {
                copy.last_modified = h.value;
            }
        }
        copy.feed = std::move(parsed).value();
        diff_and_dispatch(copy);
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_[copy.feed_url] = std::move(copy);
        return ok();
    }

    Status unsubscribe(std::string_view feed_url) override {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.erase(std::string{feed_url});
        return ok();
    }

    Status set_state(std::string_view feed_url, std::string_view guid,
                     EpisodeState new_state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(std::string{feed_url});
        if (it == subscriptions_.end()) {
            return err(ErrorCode::InvalidArgument,
                       "Podcast subscription not found",
                       "call subscribe() first");
        }
        for (auto& ep : it->second.episodes) {
            if (same_string(ep.info.guid, guid)) {
                ep.state = new_state;
                if (callbacks_.on_episode_state) {
                    const Episode copy = ep;
                    try {
                        callbacks_.on_episode_state(copy);
                    } catch (...) {
                    }
                }
                return ok();
            }
        }
        return err(ErrorCode::InvalidArgument,
                   "Podcast episode not found",
                   "no episode with that guid in the subscription");
    }

    Status set_resume_position(std::string_view feed_url,
                               std::string_view guid,
                               std::chrono::milliseconds position) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(std::string{feed_url});
        if (it == subscriptions_.end()) {
            return err(ErrorCode::InvalidArgument, "Subscription missing", "");
        }
        for (auto& ep : it->second.episodes) {
            if (same_string(ep.info.guid, guid)) {
                ep.resume_position = position;
                return ok();
            }
        }
        return err(ErrorCode::InvalidArgument, "Episode missing", "");
    }

    Status request_download(std::string_view feed_url,
                            std::string_view guid) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(std::string{feed_url});
        if (it == subscriptions_.end()) {
            return err(ErrorCode::InvalidArgument, "Subscription missing", "");
        }
        for (auto& ep : it->second.episodes) {
            if (same_string(ep.info.guid, guid)) {
                if (!ep.auto_download) ep.auto_download = true;
                ep.download_in_flight = !downloads_paused_;
                if (ep.download_in_flight) {
                    // The real download worker is wired in a follow-up
                    // commit; today we emit the progress + finish events
                    // synchronously so the host sees the contract.
                    if (callbacks_.on_download_progress) {
                        try {
                            callbacks_.on_download_progress(
                                ep, 0,
                                ep.info.enclosure
                                    ? static_cast<std::size_t>(ep.info.enclosure->length_bytes)
                                    : 0);
                        } catch (...) {
                        }
                    }
                    if (callbacks_.on_download_finished) {
                        const Episode copy = ep;
                        try {
                            callbacks_.on_download_finished(copy);
                        } catch (...) {
                        }
                    }
                    ep.download_in_flight = false;
                }
                return ok();
            }
        }
        return err(ErrorCode::InvalidArgument, "Episode missing", "");
    }

    void pause_downloads() noexcept override { downloads_paused_ = true; }
    void resume_downloads() noexcept override { downloads_paused_ = false; }

    std::optional<PodcastSubscription> subscription(
        std::string_view feed_url) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscriptions_.find(std::string{feed_url});
        if (it == subscriptions_.end()) return std::nullopt;
        return it->second;
    }

    void set_callbacks(PodcastCallbacks callbacks) noexcept override {
        callbacks_ = std::move(callbacks);
    }

    /// Wire the HTTP client. The queue is not constructed with one because
    /// the dependency direction is queue -> port, not the other way around.
    void set_http(IHttpClient* http) noexcept { http_ = http; }

  private:
    void diff_and_dispatch(PodcastSubscription& sub) {
        std::vector<Episode> next;
        next.reserve(sub.feed.episodes.size());
        for (const auto& e : sub.feed.episodes) {
            Episode ep;
            ep.info = e;
            // Preserve state for episodes that were already known.
            for (const auto& old : sub.episodes) {
                if (old.info.identity_hash == e.identity_hash) {
                    ep.state = old.state;
                    ep.resume_position = old.resume_position;
                    ep.local_path = old.local_path;
                    ep.auto_download = old.auto_download;
                    break;
                }
            }
            const bool is_new = [&]() {
                for (const auto& old : sub.episodes) {
                    if (old.info.identity_hash == e.identity_hash) return false;
                }
                return true;
            }();
            if (is_new && callbacks_.on_episode_added) {
                try {
                    callbacks_.on_episode_added(ep);
                } catch (...) {
                }
            }
            next.push_back(std::move(ep));
        }
        // Apply retention policy (REQ-NET-021).
        if (sub.keep_count > 0) {
            std::size_t played = 0;
            for (auto& ep : next) {
                if (ep.state == EpisodeState::Played) ++played;
            }
            while (played > sub.keep_count) {
                // Find the oldest played episode by pub_date_epoch (or
                // identity_hash as a tiebreak) and drop it.
                auto oldest = next.end();
                for (auto it = next.begin(); it != next.end(); ++it) {
                    if (it->state != EpisodeState::Played) continue;
                    if (oldest == next.end() ||
                        it->info.pub_date_epoch.value_or(0) <
                            oldest->info.pub_date_epoch.value_or(0)) {
                        oldest = it;
                    }
                }
                if (oldest == next.end()) break;
                oldest->state = EpisodeState::Unplayed;  // mark for removal
                --played;
            }
            next.erase(
                std::remove_if(next.begin(), next.end(),
                               [](const Episode& e) {
                                   return e.state == EpisodeState::Unplayed &&
                                          e.local_path.has_value();
                               }),
                next.end());
        }
        sub.episodes = std::move(next);
    }

    mutable std::mutex mutex_;
    std::map<std::string, PodcastSubscription> subscriptions_;
    IHttpClient* http_{nullptr};
    CancellationToken cancel_{};
    PodcastCallbacks callbacks_{};
    std::filesystem::path download_root_;
    std::atomic<bool> downloads_paused_{false};
};

std::unique_ptr<IPodcastQueue> make_default_podcast_queue(
    const std::filesystem::path& download_root) {
    auto q = std::make_unique<PodcastQueue>(download_root);
    return q;
}

}  // namespace arrow::net
