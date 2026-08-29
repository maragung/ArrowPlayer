// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Sync engine port — spec §18, REQ-SYN-001 .. REQ-SYN-014.
//
// The engine implements the full sync state machine: discovery → pairing →
// transport → change exchange. It consumes IDiscovery, IPairing, and ITransport
// ports so the real mDNS / TLS / PAKE implementations are injected at startup.
//
// The state machine is per-session: each sync session (one TLS connection to
// one paired device) follows the §7 state diagram. The engine is responsible
// for connecting, sending and receiving messages, applying the merge, and
// persisting the watermarks. Persistence (the change log, device credentials,
// tombstone horizon) is owned by the caller; the engine is stateless except
// for the in-flight session state.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "sync/ports/discovery_port.hpp"
#include "sync/ports/pairing_port.hpp"
#include "sync/ports/transport_port.hpp"

namespace arrow::sync {

/// Entities that can be synced. §1 of the protocol spec names the full set.
enum class SyncEntity {
    TrackState,   ///< play_count, skip_count, rating, is_loved, last_played, resume
    Playlist,
    PlaylistMember,
    Bookmark,
    Shortcut
};

/// Conflict resolution strategy. Per-field rules are defined in §6.1 of the
/// sync protocol; these tags are used in the Ack payload.
enum class ConflictResolution {
    Sum,       ///< counters: add deltas
    LWW,       ///< last-write-wins by lamport clock
    Max,       ///< maximum wins
    Union,     ///< additive set union
    Tombstone, ///< tombstone wins over insert
    Manual     ///< ambiguous; user must resolve
};

/// One conflict surfaced to the user after a sync session.
struct SyncConflict final {
    std::string entity;              ///< entity kind name
    std::string entity_uuid;         ///< entity UUID
    std::string field;               ///< field name
    ConflictResolution resolution;    ///< how it was resolved
    std::string winner_device_uuid;  ///< empty for Sum and Union
};

/// Watermark per origin device: the highest Lamport clock we have already
/// received from each device.
using WatermarkMap = std::map<std::string, std::int64_t>;

/// A change batch on the wire. One batch per message.
struct ChangeBatch final {
    std::vector<Change> changes;
    /// True if the sender has more changes for this entity set.
    bool more{false};
    /// Per-device high-water mark within this batch.
    WatermarkMap high_water;
};

/// Outcome of one sync session.
struct SyncResult final {
    std::string peer_uuid;
    std::string peer_name;
    /// How many changes were applied from the peer in this session.
    std::int64_t applied{0};
    std::int64_t skipped{0};
    std::int64_t pending{0};
    std::int64_t conflicts{0};
    /// The watermarks now held durably after this session.
    WatermarkMap watermarks;
    /// Conflicts that need user attention.
    std::vector<SyncConflict> conflict_list;
    bool ok{true};
    std::string error_detail;
};

/// Callbacks fired by the sync engine.
struct SyncCallbacks final {
    /// Fired at each state transition for UI progress.
    std::function<void(SyncState)> on_state;
    /// Fired with each applied change so the host can project into the DB.
    std::function<void(const Change&)> on_change;
    /// Fired when a session completes (success or failure).
    std::function<void(const SyncResult&)> on_result;
    /// Fired when a new pairing request arrives.
    std::function<void(std::string peer_name,
                        std::string peer_uuid,
                        std::string fingerprint)> on_pairing_request;
    /// Fatal error — the engine will stop.
    std::function<void(const Error&)> on_error;
};

/// The sync engine. Owns the state machine, the Lamport clock, and the
/// per-session state. Stateless for the change log and watermarks: those
/// are passed in and out so the host controls persistence.
class ISyncEngine {
  public:
    virtual ~ISyncEngine() = default;

    /// Begin a full sync session with a specific peer. The engine handles
    /// discovery (if not already resolved), pairing verification, TLS
    /// connection, the HELLO/CAPABILITIES/CHANGES handshake, and the
    /// bidirectional change exchange. Returns once the session ends.
    [[nodiscard]] virtual Status sync_with(
        const PeerDescriptor& peer,
        std::vector<SyncEntity> entities,
        std::int64_t max_changes_per_message,
        std::chrono::seconds tombstone_horizon,
        WatermarkMap my_watermarks,
        const std::vector<Change>& local_changes) = 0;

    /// Accept an incoming pairing request (the caller saw on_pairing_request).
    [[nodiscard]] virtual Status accept_pairing(std::string_view peer_uuid) = 0;

    /// Cancel the current sync session or pairing attempt. Idempotent.
    virtual void cancel() noexcept = 0;

    /// The current state machine state.
    [[nodiscard]] virtual SyncState state() const noexcept = 0;

    /// The current Lamport clock. Incremented locally before every change.
    [[nodiscard]] virtual Lamport lamport_clock() const noexcept = 0;

    /// Advance the Lamport clock to max(local, remote) + 1.
    virtual void tick_lamport(std::uint64_t remote_clock,
                              std::string_view remote_device) = 0;

    /// Snapshot of the last sync result for this peer. Empty if never synced.
    [[nodiscard]] virtual std::optional<SyncResult> last_result(
        std::string_view peer_uuid) const = 0;

    /// Wire callbacks.
    virtual void set_callbacks(SyncCallbacks callbacks) noexcept = 0;

    /// Whether the global sync enable flag is on. If false, sync_with()
    /// returns ErrorCode::NetworkDisabled without doing anything.
    [[nodiscard]] virtual bool enabled() const noexcept = 0;

    /// Set the global enable flag. Network features are off by default
    /// (REQ-NET-001).
    virtual void set_enabled(bool on) noexcept = 0;
};

/// Factory. `discovery` and `pairing` are owned by the host; the engine
/// holds non-owning pointers to them. `device_uuid`, `device_name`, and
/// `app_version` are used in the HELLO message.
[[nodiscard]] std::unique_ptr<ISyncEngine> make_sync_engine(
    IDiscovery& discovery,
    IPairing& pairing,
    std::string device_uuid,
    std::string device_name,
    std::string app_version);

}  // namespace arrow::sync
