// SPDX-License-Identifier: MPL-2.0
#include "library/change_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

namespace arrow::library {

ChangeLog::ChangeLog(Database& db, std::string device_id)
      : db_{db}, device_id_{std::move(device_id)}, clock_{0} {}

// The clock starts at 0. On first append, the returned value is 1.
int64_t ChangeLog::next_clock() {
    std::lock_guard lock{clock_mutex_};
    return ++clock_;
}

int64_t ChangeLog::clock() const {
    std::lock_guard lock{clock_mutex_};
    return clock_;
}

void ChangeLog::set_clock(int64_t clock) {
    std::lock_guard lock{clock_mutex_};
    clock_ = clock;
}

Status ChangeLog::record_insert(const std::string& entity_type,
                              int64_t entity_id,
                              std::string_view payload) {
    return record(entity_type, entity_id, "insert", payload);
}

Status ChangeLog::record_update(const std::string& entity_type,
                               int64_t entity_id,
                               std::string_view payload) {
    return record(entity_type, entity_id, "update", payload);
}

Status ChangeLog::record_delete(const std::string& entity_type,
                               int64_t entity_id) {
    return record(entity_type, entity_id, "delete", {});
}

Status ChangeLog::record(const std::string& entity_type,
                         int64_t entity_id,
                         const std::string& operation,
                         std::string_view payload) {
    int64_t c;
    {
        std::lock_guard lock{clock_mutex_};
        c = ++clock_;
    }
    return db_.append_change_log(c, device_id_, entity_type,
                                 std::to_string(entity_id), operation, payload);
}

Result<std::vector<ChangeLogEntry>> ChangeLog::get_changes_since(
    int64_t since_clock,
    int64_t limit) const {
    return db_.get_change_log_since(since_clock, limit);
}

Status ChangeLog::sync_clock() {
    auto result = db_.change_log_clock();
    if (!result) return result.error();
    std::lock_guard lock{clock_mutex_};
    clock_ = std::max(clock_, result.value());
    return ok();
}

// ===========================================================================
//  Entity helpers
// ===========================================================================

Status ChangeLog::track_inserted(int64_t track_id,
                                const TrackRecord& track) {
    // Build minimal payload: columns that differ from a default-constructed track
    json::Object payload;
    payload["container_path"] = json::Value{track.container_path};
    payload["title"] = json::Value{track.title};
    payload["artist"] = json::Value{track.artist};
    payload["album"] = json::Value{track.album};
    payload["duration_ms"] = json::Value{static_cast<double>(track.duration_ms)};
    return record_insert("track", track_id, json::Value{std::move(payload)}.dump(0));
}

Status ChangeLog::track_updated(int64_t track_id,
                               const TrackRecord& before,
                               const TrackRecord& after) {
    json::Object columns;
    if (before.title != after.title) columns["title"] = json::Value{after.title};
    if (before.artist != after.artist) columns["artist"] = json::Value{after.artist};
    if (before.album != after.album) columns["album"] = json::Value{after.album};
    if (before.rating != after.rating) columns["rating"] = json::Value{after.rating};
    if (before.is_loved != after.is_loved) columns["is_loved"] = json::Value{after.is_loved};
    if (before.play_count != after.play_count) columns["play_count"] = json::Value{after.play_count};
    if (before.last_played_at != after.last_played_at) columns["last_played_at"] = json::Value{after.last_played_at};

    if (columns.empty()) return ok();  // no-op

    json::Object payload;
    payload["columns"] = json::Value{std::move(columns)};
    return record_update("track", track_id, json::Value{std::move(payload)}.dump(0));
}

Status ChangeLog::track_deleted(int64_t track_id) {
    return record_delete("track", track_id);
}

Status ChangeLog::playlist_created(int64_t playlist_id,
                                  const PlaylistRecord& playlist) {
    json::Object payload;
    payload["uuid"] = json::Value{playlist.uuid};
    payload["name"] = json::Value{playlist.name};
    payload["kind"] = json::Value{static_cast<double>(playlist.kind)};
    return record_insert("playlist", playlist_id, json::Value{std::move(payload)}.dump(0));
}

Status ChangeLog::custom_tag_set(int64_t track_id,
                                std::string_view key,
                                std::string_view value) {
    json::Object payload;
    payload["key"] = json::Value{std::string{key}};
    payload["value"] = json::Value{std::string{value}};
    return record_update("custom_tag", track_id, json::Value{std::move(payload)}.dump(0));
}

}  // namespace arrow::library
