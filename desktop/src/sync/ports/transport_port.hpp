// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync transport port — spec §18.3 / §18.4, REQ-SYN-007 .. REQ-SYN-011.
//
// The transport is the post-pairing pipe. It carries length-prefixed
// records (CBOR or JSON; the spec defers the choice to the shared-spec
// ADR, so the port carries opaque byte buffers and leaves the framing
// to the host).
//
// The interesting surface is the change-log merge. Every change has a
// Lamport clock; the merge rules of REQ-SYN-009 / REQ-SYN-010 are
// implemented in `apply_change` and tested independently of the wire
// transport.

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace arrow::sync {

/// A Lamport-clock timestamp. The high 32 bits are the logical clock;
/// the low 32 bits are the device id (a stable hash of the device UUID,
/// truncated). Pair-compare is total: (clock, device) ordered
/// lexicographically, which is what REQ-SYN-008 requires.
struct Lamport final {
    std::uint64_t clock{0};
    std::uint32_t device{0};
    friend bool operator==(const Lamport& a, const Lamport& b) noexcept {
        return a.clock == b.clock && a.device == b.device;
    }
    friend bool operator!=(const Lamport& a, const Lamport& b) noexcept {
        return !(a == b);
    }
    friend bool operator<(const Lamport& a, const Lamport& b) noexcept {
        if (a.clock != b.clock) return a.clock < b.clock;
        return a.device < b.device;
    }
    friend bool operator>(const Lamport& a, const Lamport& b) noexcept {
        return b < a;
    }
    friend bool operator<=(const Lamport& a, const Lamport& b) noexcept {
        return !(b < a);
    }
    friend bool operator>=(const Lamport& a, const Lamport& b) noexcept {
        return !(a < b);
    }
};

/// The set of fields a sync change can carry. REQ-SYN-003 names the
/// minimum viable set: playlists, play counts, ratings, loved flags,
/// last-played timestamps, smart-playlist rule sets.
enum class ChangeField {
    PlayCount,
    SkipCount,
    Rating,
    IsLoved,
    LastPlayedAt,
    ResumePositionMs,
    PlaylistMembership,
    PlaylistRename,
    SmartPlaylistRule,
    Bookmark
};

/// A change to a single entity.
struct Change final {
    /// "track:<musicbrainz id>" or "playlist:<uuid>" — the entity being
    /// changed. The format is opaque to the port; the host picks the
    /// naming scheme and stays consistent.
    std::string entity_id;
    ChangeField field{};
    /// Lamport timestamp. The merge is order-sensitive: a change with
    /// a later timestamp is the one that wins, with the exceptions
    /// spelled out in REQ-SYN-009 (counters sum, tombstones).
    Lamport lamport{};
    /// The new value. Numeric fields are encoded as int64; string fields
    /// are encoded as their string form; for playlist membership the
    /// value is the track id being added. To remove a track from a
    /// playlist, set `tombstone = true` and the `value` is the track id
    /// to drop (REQ-SYN-009).
    std::string value;
    /// True iff this change is a tombstone (a deletion of `value` from
    /// `entity_id`). Defaults to false so adding a change stays a
    /// single-line call.
    bool tombstone{false};
    /// Device id the change originated from, used to sum counters.
    std::uint32_t origin_device{0};
};

/// Snapshot of the synced state. The host projects this onto whatever
/// local store the rest of Arrow reads from.
struct SyncState final {
    /// play_count[track_id] = sum of per-device deltas.
    std::map<std::string, std::int64_t> play_count;
    std::map<std::string, std::int64_t> skip_count;
    /// rating[track_id] = the (latest by Lamport) rating value (0..5).
    std::map<std::string, std::pair<std::int32_t, Lamport>> rating;
    /// is_loved[track_id] = (bool, lamport). LWW by Lamport.
    std::map<std::string, std::pair<bool, Lamport>> is_loved;
    /// last_played_at[track_id] = max epoch ms.
    std::map<std::string, std::int64_t> last_played_at;
    /// resume_position_ms[track_id] = (value, lamport) where lamport is
    /// the lamport of the last_played_at that sourced it.
    std::map<std::string, std::pair<std::int64_t, Lamport>> resume_position;
    /// playlist_membership[playlist_id] = ordered set of track ids with
    /// tombstones (REQ-SYN-009, REQ-SYN-010).
    std::map<std::string, std::set<std::string>> playlist_membership;
    std::map<std::string, std::map<std::string, Lamport>> playlist_tombstones;
    std::map<std::string, std::pair<std::string, Lamport>> playlist_rename;
    /// smart_rules[playlist_id] = (rule_text, lamport); the LWW rule.
    std::map<std::string, std::pair<std::string, Lamport>> smart_rules;
    /// bookmarks[track_id] = (bookmark_id -> lamport).
    std::map<std::string, std::map<std::string, Lamport>> bookmarks;
};

/// The merge. Pure function: apply every change in `changes` (which the
/// caller is expected to have de-duplicated) to `state`. Idempotent and
/// resumable per REQ-SYN-011.
[[nodiscard]] SyncState apply_changes(SyncState state,
                                      const std::vector<Change>& changes);

/// Compare two change sets. Two states are equal iff every observable
/// field is identical, regardless of the order changes were applied in.
/// Used by the §23.8 property-based convergence test.
[[nodiscard]] bool states_equal(const SyncState& a, const SyncState& b) noexcept;

/// Apply the tombstone compaction. REQ-SYN-010 says tombstones MUST
/// be retained for at least 90 days; this function is the only place
/// that drops them, and it refuses to drop anything younger than the
/// 90-day floor.
[[nodiscard]] SyncState compact_tombstones(SyncState state,
                                           std::chrono::seconds now_unix,
                                           std::chrono::seconds floor);

/// The wire transport. The port is independent of TLS — the host
/// configures it with the pre-shared key the pairing produced, and
/// applies the TLS 1.3 mutual-auth handshake via the platform's
/// implementation (which lives in a separate adapter translation unit
/// when one is wired in).
class ITransport {
  public:
    virtual ~ITransport() = default;

    /// Open a connection to `peer`. `psk` is the long-term key from
    /// pairing. The transport refuses to send any data until the TLS
    /// handshake has completed (REQ-SYN-007).
    [[nodiscard]] virtual Status connect(std::string_view host, std::uint16_t port,
                                         std::string_view psk) = 0;

    /// Send a frame. Length-prefixed; the frame body is opaque to the
    /// transport.
    [[nodiscard]] virtual Status send(std::string_view frame) = 0;

    /// Receive the next frame. Blocks until one arrives or the
    /// connection is closed.
    [[nodiscard]] virtual Result<std::string> receive() = 0;

    /// Close. Idempotent.
    virtual void close() noexcept = 0;

    /// True if the TLS handshake has completed and the peer is the
    /// one we expect (REQ-SYN-007).
    [[nodiscard]] virtual bool is_authenticated() const noexcept = 0;
};

/// Factory. Returns the in-process transport the unit tests use; the
/// production TLS 1.3 adapter replaces this when §18.4 lands.
[[nodiscard]] std::unique_ptr<ITransport> make_default_transport();

}  // namespace arrow::sync
