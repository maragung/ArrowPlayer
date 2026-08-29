// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Library database header
// Spec: eclipse-player.md §9.4, REQ-LIB-001 .. REQ-LIB-075
//
// RAII wrapper around sqlite3* that enforces:
//   - WAL mode, foreign_keys, busy_timeout on every connection (REQ-LIB-052)
//   - Single writer thread for all write operations
//   - Migration runner with numbered, ordered steps
//   - Corruption recovery (WAL checkpoint + integrity_check + .recover)
//
// This class is the ONLY entry point for database operations in the library layer.
// Do not open sqlite3* handles elsewhere in this layer.
#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "core/error.hpp"

struct sqlite3;

namespace arrow::library {

// Forward declare record types defined below
struct TrackRecord;
struct PlaylistRecord;
struct PlaylistItemRecord;
struct CustomTagRecord;
struct CuesheetRecord;
struct CueTrackRecord;
struct LibraryFolder;
struct ChangeLogEntry;

// ===========================================================================
//  Database
// ===========================================================================

/// RAII wrapper for the library SQLite database.
///
/// Thread-safety model:
///   - All reads acquire a shared lock on `mutex_`.
///   - All writes are submitted as jobs to a dedicated writer thread; the
///     calling thread blocks on a future until the writer completes.
///   - Multiple concurrent readers are safe (SQLite readers never block writers).
///
/// Corruption recovery (REQ-LIB-052):
///   - On open failure, attempts WAL recovery then reopens.
///   - If integrity_check reports corruption, attempts .recover virtual table.
///   - Returns DatabaseCorrupt if recovery fails.
///
/// Usage:
/// ```
///   Database db;
///   if (auto r = db.open("library.db"); !r) return r.error();
///   auto tracks = db.list_tracks(0, 1000);
/// ```
class Database final {
  public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    // -----------------------------------------------------------------------
    //  Lifecycle
    // -----------------------------------------------------------------------

    /// Open (or create) the database at `path`. Runs migrations. Starts the
    /// writer thread. Returns an error if the database is already open.
    [[nodiscard]] Status open(const std::filesystem::path& path);

    /// Close the database. Drains the write queue, stops the writer thread,
    /// and closes the underlying connection. Safe to call multiple times.
    void close() noexcept { shut_down(); }

    /// Returns true if the database is currently open.
    [[nodiscard]] bool is_open() const noexcept {
        std::shared_lock lock(mutex_);
        return writer();
    }

    /// Returns true if the writer thread is active.
    [[nodiscard]] bool writer() const noexcept;

    // -----------------------------------------------------------------------
    //  Low-level SQL
    // -----------------------------------------------------------------------

    /// Execute a zero-result SQL statement on the writer thread.
    [[nodiscard]] Status write(std::function<Status(sqlite3*)> fn);

    /// Execute a zero-result SQL statement synchronously (read path only).
    [[nodiscard]] Status execute(const std::string_view sql) const;

    /// WAL checkpoint — call periodically to persist WAL writes to the main db.
    [[nodiscard]] Status checkpoint() const;

    // -----------------------------------------------------------------------
    //  Schema version
    // -----------------------------------------------------------------------

    [[nodiscard]] static constexpr int schema_version() noexcept {
        return 15;
    }

    // -----------------------------------------------------------------------
    //  Library folders  (§9.4)
    // -----------------------------------------------------------------------

    /// Add a library scan root. Duplicates are rejected as ConstraintViolation.
    [[nodiscard]] Status add_library_folder(const std::filesystem::path& path);

    [[nodiscard]] Result<std::vector<LibraryFolder>> list_library_folders() const;

    // -----------------------------------------------------------------------
    //  Tracks  (REQ-LIB-001 .. REQ-LIB-031)
    // -----------------------------------------------------------------------

    /// Upsert a track record. If the path already exists, all columns are
    /// updated (including play_count / rating). Soft-deleted tracks are NOT
    /// updated — a new insert is used for resurrection.
    [[nodiscard]] Status upsert_track(const TrackRecord& track);

    /// List all non-deleted tracks, paginated by id offset.
    [[nodiscard]] Result<std::vector<TrackRecord>> list_tracks(int64_t since_id = 0,
                                                              int64_t limit = 10000) const;

    /// Get a single track by path.
    [[nodiscard]] Result<TrackRecord> get_track_by_path(std::string_view path) const;

    /// All paths currently in the library (for diffing during scan).
    [[nodiscard]] Result<std::vector<std::string>> get_all_paths() const;

    /// Mark paths as gone (soft-delete). Safe to call with an empty vector.
    [[nodiscard]] Status mark_paths_gone(const std::vector<std::string>& paths);

    // -----------------------------------------------------------------------
    //  Playlists  (§9.4)
    // -----------------------------------------------------------------------

    /// Upsert a playlist. Smart playlist rule JSON is stored verbatim.
    [[nodiscard]] Status upsert_playlist(const PlaylistRecord& playlist);

    [[nodiscard]] Result<std::vector<PlaylistRecord>> list_playlists() const;

    // -----------------------------------------------------------------------
    //  Playlist items
    // -----------------------------------------------------------------------

    /// Replace all items of a playlist with a new ordered list of track IDs.
    [[nodiscard]] Status replace_playlist_items(int64_t playlist_id,
                                              const std::vector<int64_t>& track_ids);

    [[nodiscard]] Result<std::vector<PlaylistItemRecord>> list_playlist_items(
        int64_t playlist_id) const;

    // -----------------------------------------------------------------------
    //  Custom tags  (REQ-LIB-033)
    // -----------------------------------------------------------------------

    /// Set a custom tag (key, value). Multiple values per key are allowed.
    [[nodiscard]] Status set_custom_tag(int64_t track_id,
                                       std::string_view key,
                                       std::string_view value);

    [[nodiscard]] Result<std::vector<CustomTagRecord>> get_custom_tags(
        int64_t track_id) const;

    // -----------------------------------------------------------------------
    //  Cue sheets  (REQ-LIB-040 .. REQ-LIB-044)
    // -----------------------------------------------------------------------

    [[nodiscard]] Status upsert_cuesheet(const CuesheetRecord& cue);

    [[nodiscard]] Status replace_cuetracks(const std::string& cuesheet_id,
                                         const std::vector<CueTrackRecord>& tracks);

    [[nodiscard]] Result<std::vector<CueTrackRecord>> get_cuetracks(
        const std::string& cuesheet_id) const;

    // -----------------------------------------------------------------------
    //  Sync change log  (REQ-LIB-075)
    // -----------------------------------------------------------------------

    /// Returns the current max lamport_clock, or 0 if the log is empty.
    [[nodiscard]] Result<int64_t> change_log_clock() const;

    /// Append one entry to the change log.
    [[nodiscard]] Status append_change_log(int64_t lamport_clock,
                                         std::string_view device_id,
                                         std::string_view entity_type,
                                         std::string_view entity_id,
                                         std::string_view operation,
                                         std::string_view payload);

    /// Fetch all change log entries with lamport_clock > `since_clock`.
    [[nodiscard]] Result<std::vector<ChangeLogEntry>> get_change_log_since(
        int64_t since_clock,
        int64_t limit = 1000) const;

  private:
    /// Internal: start the writer thread.
    [[nodiscard]] Status start_writer(const std::filesystem::path& path);

    /// Internal: shut down writer thread and close connection.
    void shut_down();

    /// Internal: execute SQL on a specific connection (for writer thread).
    [[nodiscard]] Status execute(sqlite3* db, const std::string_view sql) const;

    /// Internal: read a TrackRecord from the current row of a prepared stmt.
    [[nodiscard]] TrackRecord read_track_row(sqlite3_stmt* stmt) const;

    mutable std::shared_mutex mutex_;        // protects conn_
    mutable std::mutex write_mutex_;          // protects write_queue_
    std::condition_variable_any write_cv_;    // writer signal
    std::jthread writer_thread_;             // single writer thread

    struct SqliteConnection;
    SqliteConnection conn_;                   // RAII handle; guarded by mutex_

    struct WriteJob;
    std::queue<WriteJob> write_queue_;
};

// ===========================================================================
//  Record types — mirror the schema.sql table definitions exactly.
//  These must stay in sync with the Android Room entities.
// ===========================================================================

/// A library scan root. §9.4.
struct LibraryFolder final {
    int64_t id{0};
    std::string path;
    bool enabled{true};
    std::string scan_state{"idle"};
    int64_t last_scan_at{0};
    int64_t created_at{0};
};

/// The full track record. §9.4. Every column from the tracks table.
struct TrackRecord final {
    int64_t id{0};
    std::string container_path;
    std::string filename;
    int64_t file_size{0};
    int64_t duration_ms{0};
    // Metadata (REQ-LIB-025)
    std::string title;
    std::string artist;
    std::string album;
    std::string albumartist;
    std::string genre;
    int year{0};
    std::string tracknumber;
    std::string discnumber;
    std::string comment;
    double bpm{0.0};
    std::string composer;
    std::string grouping;
    std::string music_key;
    // ReplayGain (REQ-LIB-027)
    double rg_track_gain{0.0};
    double rg_track_peak{0.0};
    double rg_album_gain{0.0};
    double rg_album_peak{0.0};
    // Audio properties
    int bitrate_kbps{0};
    int sample_rate{0};
    int bit_depth{0};
    int channels{0};
    std::string codec;
    std::string container;
    bool is_lossless{false};
    // Artwork (REQ-LIB-032)
    std::string artwork_id;
    bool has_artwork{false};
    // Lyrics
    std::string lyrics_id;
    bool has_lyrics{false};
    // Source
    std::string source_id;
    std::string source_path;
    // User state (REQ-LIB-070)
    int rating{0};
    bool is_loved{false};
    bool is_blacklisted{false};
    int64_t play_count{0};
    int64_t skip_count{0};
    int64_t last_played_at{0};
    // Sort keys (REQ-LIB-029)
    std::string artist_sort_key;
    std::string album_sort_key;
    // Album identity (REQ-LIB-031)
    std::string album_hash;
    // Cue sheet (REQ-LIB-040)
    std::string cuesheet_id;
    // Timestamps
    int64_t added_at{0};
    int64_t updated_at{0};
    // File identity (symlink loop detection, §7.5)
    uint64_t file_device{0};
    uint64_t file_inode{0};
};

/// A playlist. §9.4.
struct PlaylistRecord final {
    int64_t id{0};
    std::string uuid;
    std::string name;
    std::string description;
    int kind{0};             // 0=manual, 1=smart
    std::string rule_json;   // NULL for manual playlists
    bool auto_refresh{true};
    int64_t sort_order{0};
    int64_t created_at{0};
    int64_t updated_at{0};
};

/// One item in a playlist. §9.4.
struct PlaylistItemRecord final {
    int64_t playlist_id{0};
    int64_t position{0};
    int64_t track_id{0};
    int64_t added_at{0};
};

/// A custom / user-defined tag value. §9.4 (REQ-LIB-033).
struct CustomTagRecord final {
    int64_t track_id{0};
    std::string key;
    std::string value;
    int64_t updated_at{0};
};

/// A parsed cue sheet. §9.4 (REQ-LIB-040).
struct CuesheetRecord final {
    std::string id;               // SHA-256 of the .cue file content
    std::string file_path;        // path to the .cue file on disk
    uint64_t file_device{0};
    uint64_t file_inode{0};
    std::string performer;
    std::string title;
    std::string file_ref;        // FILE block name
    bool file_is_flac{false};
    std::string catalog;
    int64_t created_at{0};
};

/// One entry (track) within a cue sheet. §9.4 (REQ-LIB-042).
struct CueTrackRecord final {
    int64_t id{0};
    std::string cuesheet_id;
    int64_t position{0};
    std::string title;
    std::string performer;
    int64_t start_offset{0};   // CD-DA frames (75 frames/second)
    int64_t end_offset{0};     // 0 = EOF
    std::string isrc;
    std::string flags;
};

/// One row in the sync change log. §9.4 (REQ-LIB-075).
struct ChangeLogEntry final {
    int64_t id{0};
    int64_t lamport_clock{0};
    std::string device_id;
    std::string entity_type;  // 'track' | 'playlist' | 'playlist_item' | 'custom_tag'
    std::string entity_id;
    std::string operation;     // 'insert' | 'update' | 'delete'
    std::string payload;       // JSON or empty on delete
    int64_t applied_at{0};
};

}  // namespace arrow::library
