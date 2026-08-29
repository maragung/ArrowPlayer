// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Sync change log
// Spec: §9.4 (REQ-LIB-075), §18 (sync protocol)
//
// Change log is a full-history append-only log. Each entry has a Lamport clock
// (logical timestamp) that provides total ordering across devices.
//
// Key invariants:
//   - clock values are monotonically increasing within a session
//   - a device's local clock is synced with the DB on startup (sync_clock)
//   - conflict resolution uses last-write-wins based on clock value (§18.3)
//
// Usage:
//   ChangeLog log{db, "device-uuid"};
//   log.sync_clock();  // on startup
//   log.record_insert("track", track_id, "{}");
//   auto changes = log.get_changes_since(last_sync_clock);
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/json/json.hpp"
#include "library/database.hpp"

namespace arrow::library {

// ===========================================================================
//  Change log
// ===========================================================================

/// A Lamport-clock-based change log for sync.
///
/// This class wraps a Database and provides:
///   - A monotonically increasing logical clock (Lamport clock)
///   - Methods to record insertions, updates, and deletions
///   - Methods to retrieve changes since a given clock value
///   - Helpers for entity-specific operations (track, playlist, custom_tag)
///
/// Thread-safety: safe to use from multiple threads concurrently.
/// The clock is protected by a mutex; the database write path uses Database::write().
class ChangeLog final {
  public:
    /// Construct a change log backed by `db`. `device_id` is the unique
    /// identifier for this device (used in the device_id column).
    explicit ChangeLog(Database& db, std::string device_id);

    // -----------------------------------------------------------------------
    //  Clock management
    // -----------------------------------------------------------------------

    /// Returns the next clock value and increments the local clock.
    /// Thread-safe: uses a mutex.
    [[nodiscard]] int64_t next_clock();

    /// Returns the current local clock value.
    [[nodiscard]] int64_t clock() const;

    /// Set the clock to a specific value. Used only for testing.
    void set_clock(int64_t clock);

    /// Sync the local clock with the database. Call on startup to ensure
    /// this device's clock is at least as large as any clock in the DB.
    /// This prevents clock regression after a crash or restart.
    [[nodiscard]] Status sync_clock();

    // -----------------------------------------------------------------------
    //  Recording changes
    // -----------------------------------------------------------------------

    /// Record an insert operation.
    [[nodiscard]] Status record_insert(const std::string& entity_type,
                                     int64_t entity_id,
                                     std::string_view payload = {});

    /// Record an update operation.
    [[nodiscard]] Status record_update(const std::string& entity_type,
                                      int64_t entity_id,
                                      std::string_view payload = {});

    /// Record a delete operation.
    [[nodiscard]] Status record_delete(const std::string& entity_type,
                                      int64_t entity_id);

    // -----------------------------------------------------------------------
    //  Querying changes
    // -----------------------------------------------------------------------

    /// Fetch all change log entries with lamport_clock > `since_clock`.
    /// Entries are returned in ascending clock order.
    [[nodiscard]] Result<std::vector<ChangeLogEntry>> get_changes_since(
        int64_t since_clock,
        int64_t limit = 1000) const;

    // -----------------------------------------------------------------------
    //  Entity helpers
    // -----------------------------------------------------------------------

    /// Record a track insertion.
    [[nodiscard]] Status track_inserted(int64_t track_id, const TrackRecord& track);

    /// Record a track update. Only records columns that differ between
    /// `before` and `after`. Skips if no columns differ.
    [[nodiscard]] Status track_updated(int64_t track_id,
                                      const TrackRecord& before,
                                      const TrackRecord& after);

    /// Record a track deletion.
    [[nodiscard]] Status track_deleted(int64_t track_id);

    /// Record a playlist creation.
    [[nodiscard]] Status playlist_created(int64_t playlist_id, const PlaylistRecord& playlist);

    /// Record a custom tag change.
    [[nodiscard]] Status custom_tag_set(int64_t track_id,
                                       std::string_view key,
                                       std::string_view value);

  private:
    /// Internal: record a change with a specific operation.
    [[nodiscard]] Status record(const std::string& entity_type,
                              int64_t entity_id,
                              const std::string& operation,
                              std::string_view payload);

    Database& db_;
    std::string device_id_;
    mutable std::mutex clock_mutex_;
    int64_t clock_;
};

}  // namespace arrow::library
