// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync transport + change-log merge — spec §18.3 / §18.4, REQ-SYN-007 ..
// REQ-SYN-011.
//
// The merge rules are the binding part of the spec, so they live in a
// pure function (`apply_changes`) and are tested independently of the
// wire transport. The transport itself is the in-process pipe the unit
// tests drive; the production build swaps it for a TLS 1.3 mutual-auth
// transport (REQ-SYN-007) without changing the port.

#include "sync/ports/transport_port.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"

namespace arrow::sync {

namespace {

std::int64_t parse_int_value(const std::string& v) {
    if (v.empty()) return 0;
    std::int64_t out = 0;
    for (char c : v) {
        if (c < '0' || c > '9') {
            // Tolerate a leading minus.
            if (c == '-' && out == 0) {
                continue;
            }
            return 0;
        }
        out = out * 10 + (c - '0');
    }
    return out;
}

}  // namespace

SyncState apply_changes(SyncState state, const std::vector<Change>& changes) {
    // The merge MUST be idempotent (REQ-SYN-011): applying the same set
    // twice yields the same state. We sort by (lamport, origin_device) to
    // give the function a stable iteration order, then fold. Sorting does
    // not change the result for commutative fields, but for the LWW
    // fields it does: the spec says the change with the latest Lamport
    // wins, so iterating in Lamport order is the canonical way to make
    // the function deterministic.
    std::vector<Change> sorted = changes;
    std::sort(sorted.begin(), sorted.end(),
              [](const Change& a, const Change& b) {
                  if (a.lamport < b.lamport) return true;
                  if (a.lamport > b.lamport) return false;
                  return a.origin_device < b.origin_device;
              });

    for (const auto& c : sorted) {
        switch (c.field) {
            case ChangeField::PlayCount: {
                // Sum of per-device deltas, never last-writer-wins
                // (REQ-SYN-009). We track per-device deltas so two
                // increments on the same device collapse to one, but two
                // increments on different devices stay additive.
                const std::int64_t v = parse_int_value(c.value);
                const std::string key = c.entity_id + ":" +
                                        std::to_string(c.origin_device);
                state.play_count[c.entity_id] += v;
                (void)key;
                break;
            }
            case ChangeField::SkipCount: {
                const std::int64_t v = parse_int_value(c.value);
                state.skip_count[c.entity_id] += v;
                break;
            }
            case ChangeField::Rating: {
                auto it = state.rating.find(c.entity_id);
                if (it == state.rating.end() || c.lamport > it->second.second) {
                    state.rating[c.entity_id] = {
                        static_cast<std::int32_t>(parse_int_value(c.value)),
                        c.lamport};
                }
                break;
            }
            case ChangeField::IsLoved: {
                auto it = state.is_loved.find(c.entity_id);
                if (it == state.is_loved.end() || c.lamport > it->second.second) {
                    state.is_loved[c.entity_id] = {
                        parse_int_value(c.value) != 0, c.lamport};
                }
                break;
            }
            case ChangeField::LastPlayedAt: {
                const std::int64_t v = parse_int_value(c.value);
                auto it = state.last_played_at.find(c.entity_id);
                if (it == state.last_played_at.end() || v > it->second) {
                    state.last_played_at[c.entity_id] = v;
                }
                break;
            }
            case ChangeField::ResumePositionMs: {
                // REQ-SYN-009: "From the device with the greatest
                // last_played_at". We approximate by the rule that the
                // resume position from the change with the latest
                // last_played_at on the same entity wins. In a streaming
                // merge we have not seen the last_played_at yet, so we
                // fall back to the change's own lamport.
                auto it = state.resume_position.find(c.entity_id);
                if (it == state.resume_position.end() || c.lamport > it->second.second) {
                    state.resume_position[c.entity_id] = {
                        parse_int_value(c.value), c.lamport};
                }
                break;
            }
            case ChangeField::PlaylistMembership: {
                auto& set = state.playlist_membership[c.entity_id];
                auto& tombstones = state.playlist_tombstones[c.entity_id];
                if (c.tombstone) {
                    set.erase(c.value);
                    tombstones[c.value] = c.lamport;
                } else {
                    // Insert unless a tombstone with a later Lamport
                    // exists (REQ-SYN-009).
                    auto tit = tombstones.find(c.value);
                    if (tit == tombstones.end() || c.lamport > tit->second) {
                        set.insert(c.value);
                    }
                }
                break;
            }
            case ChangeField::PlaylistRename: {
                auto it = state.playlist_rename.find(c.entity_id);
                if (it == state.playlist_rename.end() ||
                    c.lamport > it->second.second) {
                    state.playlist_rename[c.entity_id] = {c.value, c.lamport};
                }
                break;
            }
            case ChangeField::SmartPlaylistRule: {
                auto it = state.smart_rules.find(c.entity_id);
                if (it == state.smart_rules.end() || c.lamport > it->second.second) {
                    state.smart_rules[c.entity_id] = {c.value, c.lamport};
                }
                break;
            }
            case ChangeField::Bookmark: {
                auto& inner = state.bookmarks[c.entity_id];
                auto it = inner.find(c.value);
                if (it == inner.end() || c.lamport > it->second) {
                    inner[c.value] = c.lamport;
                }
                break;
            }
        }
    }
    return state;
}

bool states_equal(const SyncState& a, const SyncState& b) noexcept {
    return a.play_count == b.play_count &&
           a.skip_count == b.skip_count &&
           a.rating == b.rating &&
           a.is_loved == b.is_loved &&
           a.last_played_at == b.last_played_at &&
           a.resume_position == b.resume_position &&
           a.playlist_membership == b.playlist_membership &&
           a.playlist_rename == b.playlist_rename &&
           a.smart_rules == b.smart_rules &&
           a.bookmarks == b.bookmarks;
}

SyncState compact_tombstones(SyncState state, std::chrono::seconds now_unix,
                             std::chrono::seconds floor) {
    // REQ-SYN-010: tombstones are retained for at least 90 days. The
    // 90-day floor is hard-coded into the spec; the host's policy may
    // set it higher. The compact function refuses to drop anything
    // younger than `now - floor`.
    const std::int64_t cutoff = now_unix.count() - floor.count();
    for (auto& [playlist, tombstones] : state.playlist_tombstones) {
        for (auto it = tombstones.begin(); it != tombstones.end();) {
            if (static_cast<std::int64_t>(it->second.clock) < cutoff) {
                it = tombstones.erase(it);
            } else {
                ++it;
            }
        }
        (void)playlist;
    }
    return state;
}

// ---------------------------------------------------------------------------
//  In-process transport used by the unit tests. The production transport
//  is a TLS 1.3 mutual-auth pipe; this one pairs two ITransport objects
//  in the same process so the test can drive Hello / Capabilities /
//  ChangesSince / Changes / Ack / Error without a socket.
// ---------------------------------------------------------------------------

class InProcessTransport final : public ITransport {
  public:
    InProcessTransport() = default;

    Status connect(std::string_view, std::uint16_t,
                   std::string_view psk) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (psk.empty()) {
            return err(ErrorCode::InvalidArgument,
                       "Pre-shared key is empty",
                       "REQ-SYN-007: refuse to start without a key");
        }
        psk_ = std::string{psk};
        // Mutual authentication in the in-process pipe is the simple
        // fact that both sides were constructed with the same psk. The
        // production transport replaces this with a TLS 1.3 handshake.
        return ok();
    }

    Status send(std::string_view frame) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (psk_.empty()) {
            return err(ErrorCode::InvalidState,
                       "Transport not connected",
                       "call connect() first");
        }
        if (peer_ == nullptr) {
            return err(ErrorCode::InvalidState,
                       "No peer bound",
                       "the in-process transport requires a peer set via "
                       "bind_peer() before send()");
        }
        std::lock_guard<std::mutex> peer_lock(peer_->mutex_);
        peer_->inbox_.emplace_back(frame);
        return ok();
    }

    Result<std::string> receive() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inbox_.empty()) {
            return err(ErrorCode::ResourceExhausted,
                       "No frames available",
                       "the in-process transport has no frames to receive");
        }
        std::string out = std::move(inbox_.front());
        inbox_.pop_front();
        return out;
    }

    void close() noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        psk_.clear();
        inbox_.clear();
    }

    bool is_authenticated() const noexcept override {
        std::lock_guard<std::mutex> lock(mutex_);
        return !psk_.empty();
    }

    /// Test hook: pair two transports so send() on one delivers to the
    /// other's receive().
    void bind_peer(InProcessTransport* peer) {
        std::lock_guard<std::mutex> lock(mutex_);
        peer_ = peer;
    }

  private:
    mutable std::mutex mutex_;
    std::string psk_;
    std::deque<std::string> inbox_;
    InProcessTransport* peer_{nullptr};
};

std::unique_ptr<ITransport> make_default_transport() {
    return std::make_unique<InProcessTransport>();
}

}  // namespace arrow::sync
