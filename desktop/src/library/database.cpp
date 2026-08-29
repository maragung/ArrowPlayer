// SPDX-License-Identifier: MPL-2.0
#include "library/database.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <sqlite3.h>
#include <thread>
#include <vector>

#include "core/error.hpp"

namespace arrow::library {

namespace {

// ---------------------------------------------------------------------------
//  RAII sqlite3 connection wrapper
// ---------------------------------------------------------------------------

class SqliteConnection {
  public:
    SqliteConnection() = default;

    explicit SqliteConnection(const std::filesystem::path& path) { open(path); }

    ~SqliteConnection() { close(); }

    SqliteConnection(SqliteConnection&& other) noexcept
          : db_{std::exchange(other.db_, nullptr)} {}

    SqliteConnection& operator=(SqliteConnection&& other) noexcept {
        if (this != &other) {
            close();
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }

    [[nodiscard]] Status open(const std::filesystem::path& path) {
        close();
        if (path.empty()) {
            return err(ErrorCode::InvalidArgument, "The database path is empty.");
        }

        const auto parent = path.parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return err(ErrorCode::IoError, "Could not create database directory.", ec.message());
            }
        }

        const int rc = sqlite3_open_v2(
            path.string().c_str(), &db_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
        if (rc != SQLITE_OK) {
            const std::string detail = sqlite3_errmsg(db_);
            close();
            return err(ErrorCode::DatabaseCorrupt, "The library database could not be opened.", detail);
        }

        // PRAGMA setup (REQ-LIB-052)
        const char* pragmas[][2] = {
            {"journal_mode", "WAL"},
            {"foreign_keys", "ON"},
            {"synchronous", "NORMAL"},
            {"busy_timeout", "5000"},
            {"temp_store", "MEMORY"},
            {"mmap_size", "268435456"},
            {"wal_autocheckpoint", "100"},
        };
        for (const auto& [key, value] : pragmas) {
            std::string sql = "PRAGMA ";
            sql += key;
            sql += "=";
            sql += value;
            sql += ";";
            char* msg = nullptr;
            const int pr = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg);
            if (pr != SQLITE_OK) {
                const std::string detail = msg == nullptr ? "" : msg;
                sqlite3_free(msg);
                // WAL on network filesystems may fail; warn but do not abort
                if (std::strcmp(key, "journal_mode") != 0) {
                    close();
                    return err(ErrorCode::QueryFailed, "PRAGMA " + std::string{key} + " failed.", detail);
                }
            }
        }

        return ok();
    }

    void close() noexcept {
        if (db_ != nullptr) {
            sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    [[nodiscard]] operator sqlite3*() noexcept { return db_; }
    [[nodiscard]] operator const sqlite3*() const noexcept { return db_; }

    [[nodiscard]] std::string error_message() const {
        return db_ == nullptr ? "closed" : sqlite3_errmsg(db_);
    }

  private:
    sqlite3* db_{nullptr};
};

// ---------------------------------------------------------------------------
//  Schema version
// ---------------------------------------------------------------------------

constexpr int CURRENT_SCHEMA_VERSION = 15;

[[nodiscard]] Result<int> get_schema_version(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, "PRAGMA schema_version;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare schema_version.",
                   sqlite3_errmsg(db));
    }
    int version = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (version < 0) {
        return err(ErrorCode::DatabaseCorrupt, "schema_version pragma returned no rows.");
    }
    return version;
}

[[nodiscard]] Status set_schema_version(sqlite3* db, int version) {
    char* msg = nullptr;
    std::string sql = "PRAGMA schema_version=" + std::to_string(version) + ";";
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        const std::string detail = msg == nullptr ? sqlite3_errmsg(db) : msg;
        sqlite3_free(msg);
        return err(ErrorCode::QueryFailed, "Could not set schema version.", detail);
    }
    return ok();
}

// ---------------------------------------------------------------------------
//  Read schema SQL from file
// ---------------------------------------------------------------------------

[[nodiscard]] Result<std::string> read_schema_sql() {
    // Schema is read from schema.sql at runtime. This avoids the need for a
    // generated header or build-time preprocessing.
    // The schema.sql file is the canonical DDL; the C++ layer reads it at open.
    constexpr std::string_view kSchemaPath = "src/library/schema.sql";
    std::ifstream in{std::string{kSchemaPath}};
    if (!in) {
        return err(ErrorCode::IoError,
                   "Could not open schema.sql. Ensure it is in the working directory.");
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
    return content;
}

// ---------------------------------------------------------------------------
//  Individual migrations (run in order during startup)
// ---------------------------------------------------------------------------

Status mig_add_library_folders(sqlite3* db, void*) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS library_folders ("
        "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
        "path TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "scan_state TEXT NOT NULL DEFAULT 'idle',"
        "last_scan_at INTEGER,"
        "created_at INTEGER NOT NULL);";
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 8 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

Status mig_add_user_state(sqlite3* db, void*) {
    const char* sql =
        "ALTER TABLE tracks ADD COLUMN is_loved INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE tracks ADD COLUMN is_blacklisted INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE tracks ADD COLUMN play_count INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE tracks ADD COLUMN skip_count INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE tracks ADD COLUMN last_played_at INTEGER;";
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 9 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

Status mig_add_sort_keys(sqlite3* db, void*) {
    const char* sql =
        "ALTER TABLE tracks ADD COLUMN artist_sort_key TEXT NOT NULL DEFAULT '';"
        "ALTER TABLE tracks ADD COLUMN album_sort_key TEXT NOT NULL DEFAULT '';"
        "ALTER TABLE tracks ADD COLUMN album_hash TEXT NOT NULL DEFAULT '';";
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 11 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

Status mig_add_cuesheets(sqlite3* db, void*) {
    const char* cuesheets =
        "CREATE TABLE IF NOT EXISTS cuesheets ("
        "id TEXT NOT NULL PRIMARY KEY,"
        "file_path TEXT NOT NULL,"
        "file_device INTEGER NOT NULL DEFAULT 0,"
        "file_inode INTEGER NOT NULL DEFAULT 0,"
        "performer TEXT NOT NULL DEFAULT '',"
        "title TEXT NOT NULL DEFAULT '',"
        "file_ref TEXT NOT NULL,"
        "file_is_flac INTEGER NOT NULL DEFAULT 0,"
        "catalog TEXT NOT NULL DEFAULT '',"
        "created_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_cuesheets_file_path ON cuesheets(file_path);";
    const char* cuetracks =
        "CREATE TABLE IF NOT EXISTS cuetracks ("
        "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
        "cuesheet_id TEXT NOT NULL REFERENCES cuesheets(id) ON DELETE CASCADE,"
        "position INTEGER NOT NULL DEFAULT 0,"
        "title TEXT NOT NULL DEFAULT '',"
        "performer TEXT NOT NULL DEFAULT '',"
        "start_offset INTEGER NOT NULL DEFAULT 0,"
        "end_offset INTEGER NOT NULL DEFAULT 0,"
        "isrc TEXT NOT NULL DEFAULT '',"
        "flags TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX IF NOT EXISTS idx_cuetracks_cuesheet "
        "ON cuetracks(cuesheet_id, position);";
    if (sqlite3_exec(db, cuesheets, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 12/13 failed.",
                   sqlite3_errmsg(db));
    }
    if (sqlite3_exec(db, cuetracks, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 13 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

Status mig_add_playlist_sort_order(sqlite3* db, void*) {
    const char* sql =
        "ALTER TABLE playlists ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0;";
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 14 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

Status mig_add_change_log(sqlite3* db, void*) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS change_log ("
        "id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,"
        "lamport_clock INTEGER NOT NULL,"
        "device_id TEXT NOT NULL,"
        "entity_type TEXT NOT NULL,"
        "entity_id TEXT NOT NULL,"
        "operation TEXT NOT NULL,"
        "payload TEXT NOT NULL,"
        "applied_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS idx_change_log_clock ON change_log(lamport_clock);"
        "CREATE INDEX IF NOT EXISTS idx_change_log_entity "
        "ON change_log(entity_type, entity_id);"
        "CREATE INDEX IF NOT EXISTS idx_change_log_applied "
        "ON change_log(applied_at) WHERE applied_at IS NULL;"
        "CREATE INDEX IF NOT EXISTS idx_change_log_track "
        "ON change_log(entity_type, lamport_clock) WHERE entity_type = 'track';"
        "CREATE INDEX IF NOT EXISTS idx_change_log_playlist "
        "ON change_log(entity_type, lamport_clock) WHERE entity_type = 'playlist';";
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::MigrationFailed, "Migration 15 failed.",
                   sqlite3_errmsg(db));
    }
    return ok();
}

// Migration: target version → migration function
struct MigrationStep {
    int target_version;
    const char* name;
    Status (*fn)(sqlite3*, void*);
};

const MigrationStep kMigrations[] = {
    {8,  "add_library_folders",       mig_add_library_folders},
    {9,  "add_user_state",            mig_add_user_state},
    {11, "add_sort_keys",             mig_add_sort_keys},
    {13, "add_cuesheets",             mig_add_cuesheets},
    {14, "add_playlist_sort_order",   mig_add_playlist_sort_order},
    {15, "add_change_log",           mig_add_change_log},
};

// ---------------------------------------------------------------------------
//  Run all pending migrations
// ---------------------------------------------------------------------------

[[nodiscard]] Status run_migrations(sqlite3* db) {
    auto version_res = get_schema_version(db);
    if (!version_res) return version_res.error();
    int current = version_res.value();

    if (current == CURRENT_SCHEMA_VERSION) return ok();
    if (current > CURRENT_SCHEMA_VERSION) {
        return err(ErrorCode::MigrationFailed,
                   "Database schema is from a newer version.",
                   "DB version: " + std::to_string(current) +
                       ", supported: " + std::to_string(CURRENT_SCHEMA_VERSION));
    }

    for (const auto& mig : kMigrations) {
        if (mig.target_version <= current) continue;
        if (mig.target_version > CURRENT_SCHEMA_VERSION) break;

        if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            return err(ErrorCode::MigrationFailed,
                       "Migration " + std::to_string(mig.target_version) + " failed to begin.",
                       sqlite3_errmsg(db));
        }
        auto res = mig.fn(db, nullptr);
        if (!res) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return res;
        }
        auto ver_res = set_schema_version(db, mig.target_version);
        if (!ver_res) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return ver_res;
        }
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return err(ErrorCode::MigrationFailed,
                       "Migration " + std::to_string(mig.target_version) + " commit failed.",
                       sqlite3_errmsg(db));
        }
        current = mig.target_version;
    }

    return ok();
}

// ---------------------------------------------------------------------------
//  Corruption recovery  (REQ-LIB-052)
// ---------------------------------------------------------------------------

[[nodiscard]] Status recover_database(sqlite3* db, const std::filesystem::path& path) {
    // WAL checkpoint
    if (sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_RECOVER,
                                  nullptr, nullptr) != SQLITE_OK) {
        return err(ErrorCode::DatabaseCorrupt, "WAL recovery checkpoint failed.",
                   sqlite3_errmsg(db));
    }

    // Integrity check
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return err(ErrorCode::DatabaseCorrupt, "Could not prepare integrity_check.",
                   sqlite3_errmsg(db));
    }
    std::string integrity_result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* txt = sqlite3_column_text(stmt, 0);
        integrity_result = txt ? reinterpret_cast<const char*>(txt) : "";
    }
    sqlite3_finalize(stmt);

    if (integrity_result != "ok") {
        // Attempt recovery: close and retry
        sqlite3_close(db);
        // Reopen with a fresh handle
        // (caller will retry)
    }

    return ok();
}

// ---------------------------------------------------------------------------
//  Write job
// ---------------------------------------------------------------------------

struct WriteJob {
    std::function<Status(sqlite3*)> fn;
    std::promise<Status> promise;
};

}  // anonymous namespace

// ===========================================================================
//  Database  (public interface)
// ===========================================================================

Database::~Database() { shut_down(); }

Status Database::open(const std::filesystem::path& path) {
    std::unique_lock lock{mutex_};

    if (conn_.is_open()) {
        return err(ErrorCode::InvalidState, "The database is already open.");
    }

    // Open the connection
    if (auto res = conn_.open(path); !res) {
        // Attempt recovery
        SqliteConnection recovery_conn;
        if (auto rec = recovery_conn.open(path); rec && recovery_conn.is_open()) {
            if (auto fix = recover_database(recovery_conn, path); fix) {
                recovery_conn.close();
                if (auto retry = conn_.open(path); retry) {
                    if (auto mig = run_migrations(conn_); !mig) {
                        conn_.close();
                        return mig;
                    }
                    return start_writer(path);
                }
            }
        }
        return res;
    }

    // Initialize with schema
    auto schema_res = read_schema_sql();
    if (!schema_res) {
        conn_.close();
        return schema_res.error();
    }
    if (auto res = execute(conn_, schema_res.value()); !res) {
        conn_.close();
        return res;
    }

    // Run migrations from version 1 → current
    if (auto res = run_migrations(conn_); !res) {
        conn_.close();
        return res;
    }

    return start_writer(path);
}

Status Database::start_writer(const std::filesystem::path& path) {
    if (writer_thread_.joinable()) {
        return err(ErrorCode::InvalidState, "Writer thread already running.");
    }

    writer_thread_ = std::jthread([this, path](std::stop_token token) {
        SqliteConnection writer_conn;
        writer_conn.open(path);

        while (!token.stop_requested()) {
            WriteJob job;
            {
                std::unique_lock lock{write_mutex_};
                write_cv_.wait(lock, token,
                             [this] { return !write_queue_.empty() || token.stop_requested(); });
                if (token.stop_requested() && write_queue_.empty()) break;
                if (write_queue_.empty()) continue;
                job = std::move(write_queue_.front());
                write_queue_.pop();
            }

            if (job.fn) {
                Status result = [&]() -> Status {
                    std::unique_lock lk{mutex_};
                    return job.fn(conn_);
                }();
                job.promise.set_value(std::move(result));
            }
        }

        // Drain remaining jobs on shutdown
        while (true) {
            WriteJob job;
            {
                std::unique_lock lock{write_mutex_};
                if (write_queue_.empty()) break;
                job = std::move(write_queue_.front());
                write_queue_.pop();
            }
            if (job.fn) {
                Status result = [&]() -> Status {
                    std::unique_lock lk{mutex_};
                    return job.fn(conn_);
                }();
                job.promise.set_value(std::move(result));
            }
        }
    });

    return ok();
}

void Database::shut_down() {
    {
        std::unique_lock lock{write_mutex_};
        write_cv_.notify_all();
    }
    if (writer_thread_.joinable()) {
        writer_thread_.request_stop();
        writer_thread_.join();
    }
    std::unique_lock lock{mutex_};
    conn_.close();
}

bool Database::writer() const noexcept {
    return writer_thread_.joinable();
}

Status Database::write(std::function<Status(sqlite3*)> fn) {
    std::promise<Status> promise;
    auto future = promise.get_future();
    {
        std::unique_lock lock{write_mutex_};
        write_queue_.push(WriteJob{std::move(fn), std::move(promise)});
        write_cv_.notify_one();
    }
    return future.get();
}

Status Database::execute(const std::string_view sql) const {
    std::shared_lock lock{mutex_};
    return execute(conn_, sql);
}

Status Database::execute(sqlite3* db, const std::string_view sql) const {
    if (db == nullptr) {
        return err(ErrorCode::InvalidState, "The database is not open.");
    }
    const std::string terminated{sql};
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, terminated.c_str(), nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        const std::string detail = msg == nullptr ? sqlite3_errmsg(db) : msg;
        sqlite3_free(msg);
        return err(ErrorCode::QueryFailed, "The database operation failed.", detail);
    }
    return ok();
}

Status Database::checkpoint() const {
    std::shared_lock lock{mutex_};
    if (!conn_.is_open()) {
        return err(ErrorCode::InvalidState, "The database is not open.");
    }
    int pages = 0;
    if (sqlite3_wal_checkpoint_v2(conn_, nullptr, SQLITE_CHECKPOINT_PASSIVE,
                                  &pages, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "WAL checkpoint failed.", conn_.error_message());
    }
    return ok();
}

// ===========================================================================
//  Record helpers
// ===========================================================================

TrackRecord Database::read_track_row(sqlite3_stmt* stmt) const {
    TrackRecord t;
    t.id = sqlite3_column_int64(stmt, 0);
    auto get_text = [&](int idx) -> std::string {
        const auto* s = sqlite3_column_text(stmt, idx);
        return s ? reinterpret_cast<const char*>(s) : std::string{};
    };
    auto get_text_or_null = [&](int idx) -> std::string {
        if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return {};
        return get_text(idx);
    };
    t.container_path = get_text(1);
    t.filename = get_text(2);
    t.file_size = sqlite3_column_int64(stmt, 3);
    t.duration_ms = sqlite3_column_int64(stmt, 4);
    t.title = get_text(5);
    t.artist = get_text(6);
    t.album = get_text(7);
    t.albumartist = get_text(8);
    t.genre = get_text(9);
    t.year = static_cast<int>(sqlite3_column_int64(stmt, 10));
    t.tracknumber = get_text(11);
    t.discnumber = get_text(12);
    t.comment = get_text(13);
    t.bpm = sqlite3_column_double(stmt, 14);
    t.composer = get_text(15);
    t.grouping = get_text(16);
    t.music_key = get_text(17);
    t.rg_track_gain = sqlite3_column_double(stmt, 18);
    t.rg_track_peak = sqlite3_column_double(stmt, 19);
    t.rg_album_gain = sqlite3_column_double(stmt, 20);
    t.rg_album_peak = sqlite3_column_double(stmt, 21);
    t.bitrate_kbps = static_cast<int>(sqlite3_column_int64(stmt, 22));
    t.sample_rate = static_cast<int>(sqlite3_column_int64(stmt, 23));
    t.bit_depth = static_cast<int>(sqlite3_column_int64(stmt, 24));
    t.channels = static_cast<int>(sqlite3_column_int64(stmt, 25));
    t.codec = get_text(26);
    t.container = get_text(27);
    t.is_lossless = sqlite3_column_int(stmt, 28) != 0;
    t.artwork_id = get_text_or_null(29);
    t.has_artwork = sqlite3_column_int(stmt, 30) != 0;
    t.lyrics_id = get_text_or_null(31);
    t.has_lyrics = sqlite3_column_int(stmt, 32) != 0;
    t.source_id = get_text(33);
    t.source_path = get_text(34);
    t.rating = static_cast<int>(sqlite3_column_int64(stmt, 35));
    t.is_loved = sqlite3_column_int(stmt, 36) != 0;
    t.is_blacklisted = sqlite3_column_int(stmt, 37) != 0;
    t.play_count = sqlite3_column_int64(stmt, 38);
    t.skip_count = sqlite3_column_int64(stmt, 39);
    t.last_played_at = sqlite3_column_int64(stmt, 40);
    t.artist_sort_key = get_text(41);
    t.album_sort_key = get_text(42);
    t.album_hash = get_text(43);
    t.cuesheet_id = get_text_or_null(44);
    t.added_at = sqlite3_column_int64(stmt, 45);
    t.updated_at = sqlite3_column_int64(stmt, 46);
    t.file_device = static_cast<uint64_t>(sqlite3_column_int64(stmt, 47));
    t.file_inode = static_cast<uint64_t>(sqlite3_column_int64(stmt, 48));
    return t;
}

// ===========================================================================
//  Change log  (REQ-LIB-075)
// ===========================================================================

Result<int64_t> Database::change_log_clock() const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(conn_,
                           "SELECT COALESCE(MAX(lamport_clock), 0) FROM change_log;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not query change log clock.",
                   conn_.error_message());
    }
    int64_t clock = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) clock = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return clock;
}

Status Database::append_change_log(int64_t lamport_clock,
                                  std::string_view device_id,
                                  std::string_view entity_type,
                                  std::string_view entity_id,
                                  std::string_view operation,
                                  std::string_view payload) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO change_log(lamport_clock,device_id,entity_type,entity_id,"
            "operation,payload,applied_at) VALUES(?,?,?,?,?,?,?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare change log insert.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_bind_int64(stmt, 1, lamport_clock);
        sqlite3_bind_text(stmt, 2, device_id.data(),
                          static_cast<int>(device_id.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entity_type.data(),
                          static_cast<int>(entity_type.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, entity_id.data(),
                          static_cast<int>(entity_id.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, operation.data(),
                          static_cast<int>(operation.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, payload.data(),
                          static_cast<int>(payload.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 7, now);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not append change log.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Result<std::vector<ChangeLogEntry>> Database::get_change_log_since(
    int64_t since_clock, int64_t limit) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,lamport_clock,device_id,entity_type,entity_id,operation,"
        "payload,applied_at FROM change_log WHERE lamport_clock > ? "
        "ORDER BY lamport_clock ASC LIMIT ?;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare change log query.",
                   conn_.error_message());
    }
    sqlite3_bind_int64(stmt, 1, since_clock);
    sqlite3_bind_int64(stmt, 2, limit);
    std::vector<ChangeLogEntry> entries;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChangeLogEntry e;
        e.id = sqlite3_column_int64(stmt, 0);
        e.lamport_clock = sqlite3_column_int64(stmt, 1);
        const auto* did = sqlite3_column_text(stmt, 2);
        const auto* et = sqlite3_column_text(stmt, 3);
        const auto* eid = sqlite3_column_text(stmt, 4);
        const auto* op = sqlite3_column_text(stmt, 5);
        const auto* pl = sqlite3_column_text(stmt, 6);
        e.device_id = did ? reinterpret_cast<const char*>(did) : "";
        e.entity_type = et ? reinterpret_cast<const char*>(et) : "";
        e.entity_id = eid ? reinterpret_cast<const char*>(eid) : "";
        e.operation = op ? reinterpret_cast<const char*>(op) : "";
        e.payload = pl ? reinterpret_cast<const char*>(pl) : "";
        e.applied_at = sqlite3_column_int64(stmt, 7);
        entries.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
    return entries;
}

// ===========================================================================
//  Library folders
// ===========================================================================

Status Database::add_library_folder(const std::filesystem::path& path) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO library_folders(path,scan_state,created_at) VALUES(?,?,?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare folder insert.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_bind_text(stmt, 1, path.string().c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "idle", 5, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_CONSTRAINT) {
            return err(ErrorCode::ConstraintViolation, "This folder is already in the library.");
        }
        if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not add library folder.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Result<std::vector<LibraryFolder>> Database::list_library_folders() const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,path,enabled,scan_state,last_scan_at,created_at "
        "FROM library_folders ORDER BY id;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare folder list.",
                   conn_.error_message());
    }
    std::vector<LibraryFolder> folders;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LibraryFolder f;
        f.id = sqlite3_column_int64(stmt, 0);
        const auto* p = sqlite3_column_text(stmt, 1);
        const auto* ss = sqlite3_column_text(stmt, 3);
        f.path = p ? reinterpret_cast<const char*>(p) : "";
        f.enabled = sqlite3_column_int(stmt, 2) != 0;
        f.scan_state = ss ? reinterpret_cast<const char*>(ss) : "idle";
        f.last_scan_at = sqlite3_column_int64(stmt, 4);
        f.created_at = sqlite3_column_int64(stmt, 5);
        folders.push_back(std::move(f));
    }
    sqlite3_finalize(stmt);
    return folders;
}

// ===========================================================================
//  Tracks
// ===========================================================================

Status Database::upsert_track(const TrackRecord& track) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO tracks("
            "container_path,filename,file_size,duration_ms,"
            "title,artist,album,albumartist,genre,year,tracknumber,discnumber,"
            "comment,bpm,composer,grouping,music_key,"
            "rg_track_gain,rg_track_peak,rg_album_gain,rg_album_peak,"
            "bitrate_kbps,sample_rate,bit_depth,channels,codec,container,is_lossless,"
            "artwork_id,has_artwork,lyrics_id,has_lyrics,"
            "source_id,source_path,"
            "rating,is_loved,is_blacklisted,play_count,skip_count,last_played_at,"
            "artist_sort_key,album_sort_key,album_hash,cuesheet_id,"
            "added_at,updated_at,file_device,file_inode) VALUES("
            "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
            "?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare track upsert.",
                       sqlite3_errmsg(db));
        }
        auto bind_text = [&](int idx, std::string_view s) {
            sqlite3_bind_text(stmt, idx, s.data(),
                              static_cast<int>(s.size()), SQLITE_TRANSIENT);
        };
        auto bind_or_null = [&](int idx, std::string_view s) {
            if (s.empty())
                sqlite3_bind_null(stmt, idx);
            else
                sqlite3_bind_text(stmt, idx, s.data(),
                                 static_cast<int>(s.size()), SQLITE_TRANSIENT);
        };

        const auto now = static_cast<int64_t>(std::time(nullptr));
        int i = 1;
        bind_text(i++, track.container_path);
        bind_text(i++, track.filename);
        sqlite3_bind_int64(stmt, i++, track.file_size);
        sqlite3_bind_int64(stmt, i++, track.duration_ms);
        bind_text(i++, track.title);
        bind_text(i++, track.artist);
        bind_text(i++, track.album);
        bind_text(i++, track.albumartist);
        bind_text(i++, track.genre);
        sqlite3_bind_int64(stmt, i++, track.year);
        bind_text(i++, track.tracknumber);
        bind_text(i++, track.discnumber);
        bind_text(i++, track.comment);
        sqlite3_bind_double(stmt, i++, track.bpm);
        bind_text(i++, track.composer);
        bind_text(i++, track.grouping);
        bind_text(i++, track.music_key);
        sqlite3_bind_double(stmt, i++, track.rg_track_gain);
        sqlite3_bind_double(stmt, i++, track.rg_track_peak);
        sqlite3_bind_double(stmt, i++, track.rg_album_gain);
        sqlite3_bind_double(stmt, i++, track.rg_album_peak);
        sqlite3_bind_int64(stmt, i++, track.bitrate_kbps);
        sqlite3_bind_int64(stmt, i++, track.sample_rate);
        sqlite3_bind_int64(stmt, i++, track.bit_depth);
        sqlite3_bind_int64(stmt, i++, track.channels);
        bind_text(i++, track.codec);
        bind_text(i++, track.container);
        sqlite3_bind_int(stmt, i++, track.is_lossless ? 1 : 0);
        bind_or_null(i++, track.artwork_id);
        sqlite3_bind_int(stmt, i++, track.has_artwork ? 1 : 0);
        bind_or_null(i++, track.lyrics_id);
        sqlite3_bind_int(stmt, i++, track.has_lyrics ? 1 : 0);
        bind_text(i++, track.source_id);
        bind_text(i++, track.source_path);
        sqlite3_bind_int(stmt, i++, track.rating);
        sqlite3_bind_int(stmt, i++, track.is_loved ? 1 : 0);
        sqlite3_bind_int(stmt, i++, track.is_blacklisted ? 1 : 0);
        sqlite3_bind_int64(stmt, i++, track.play_count);
        sqlite3_bind_int64(stmt, i++, track.skip_count);
        if (track.last_played_at > 0)
            sqlite3_bind_int64(stmt, i++, track.last_played_at);
        else
            sqlite3_bind_null(stmt, i++);
        bind_text(i++, track.artist_sort_key);
        bind_text(i++, track.album_sort_key);
        bind_text(i++, track.album_hash);
        bind_or_null(i++, track.cuesheet_id);
        sqlite3_bind_int64(stmt, i++, track.added_at > 0 ? track.added_at : now);
        sqlite3_bind_int64(stmt, i++, now);
        sqlite3_bind_int64(stmt, i++, static_cast<int64_t>(track.file_device));
        sqlite3_bind_int64(stmt, i++, static_cast<int64_t>(track.file_inode));

        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_CONSTRAINT) {
            // UPDATE path
            const char* upd =
                "UPDATE tracks SET "
                "filename=?,file_size=?,duration_ms=?,title=?,artist=?,album=?,"
                "albumartist=?,genre=?,year=?,tracknumber=?,discnumber=?,comment=?,"
                "bpm=?,composer=?,grouping=?,music_key=?,"
                "rg_track_gain=?,rg_track_peak=?,rg_album_gain=?,rg_album_peak=?,"
                "bitrate_kbps=?,sample_rate=?,bit_depth=?,channels=?,codec=?,container=?,"
                "is_lossless=?,artwork_id=?,has_artwork=?,lyrics_id=?,has_lyrics=?,"
                "source_id=?,source_path=?,"
                "rating=?,is_loved=?,is_blacklisted=?,play_count=?,skip_count=?,"
                "last_played_at=?,artist_sort_key=?,album_sort_key=?,album_hash=?,"
                "cuesheet_id=?,updated_at=?,file_device=?,file_inode=? "
                "WHERE container_path=?;";

            if (sqlite3_prepare_v2(db, upd, -1, &stmt, nullptr) != SQLITE_OK) {
                return err(ErrorCode::QueryFailed, "Could not prepare track update.",
                           sqlite3_errmsg(db));
            }
            i = 1;
            bind_text(i++, track.filename);
            sqlite3_bind_int64(stmt, i++, track.file_size);
            sqlite3_bind_int64(stmt, i++, track.duration_ms);
            bind_text(i++, track.title);
            bind_text(i++, track.artist);
            bind_text(i++, track.album);
            bind_text(i++, track.albumartist);
            bind_text(i++, track.genre);
            sqlite3_bind_int64(stmt, i++, track.year);
            bind_text(i++, track.tracknumber);
            bind_text(i++, track.discnumber);
            bind_text(i++, track.comment);
            sqlite3_bind_double(stmt, i++, track.bpm);
            bind_text(i++, track.composer);
            bind_text(i++, track.grouping);
            bind_text(i++, track.music_key);
            sqlite3_bind_double(stmt, i++, track.rg_track_gain);
            sqlite3_bind_double(stmt, i++, track.rg_track_peak);
            sqlite3_bind_double(stmt, i++, track.rg_album_gain);
            sqlite3_bind_double(stmt, i++, track.rg_album_peak);
            sqlite3_bind_int64(stmt, i++, track.bitrate_kbps);
            sqlite3_bind_int64(stmt, i++, track.sample_rate);
            sqlite3_bind_int64(stmt, i++, track.bit_depth);
            sqlite3_bind_int64(stmt, i++, track.channels);
            bind_text(i++, track.codec);
            bind_text(i++, track.container);
            sqlite3_bind_int(stmt, i++, track.is_lossless ? 1 : 0);
            bind_or_null(i++, track.artwork_id);
            sqlite3_bind_int(stmt, i++, track.has_artwork ? 1 : 0);
            bind_or_null(i++, track.lyrics_id);
            sqlite3_bind_int(stmt, i++, track.has_lyrics ? 1 : 0);
            bind_text(i++, track.source_id);
            bind_text(i++, track.source_path);
            sqlite3_bind_int(stmt, i++, track.rating);
            sqlite3_bind_int(stmt, i++, track.is_loved ? 1 : 0);
            sqlite3_bind_int(stmt, i++, track.is_blacklisted ? 1 : 0);
            sqlite3_bind_int64(stmt, i++, track.play_count);
            sqlite3_bind_int64(stmt, i++, track.skip_count);
            if (track.last_played_at > 0)
                sqlite3_bind_int64(stmt, i++, track.last_played_at);
            else
                sqlite3_bind_null(stmt, i++);
            bind_text(i++, track.artist_sort_key);
            bind_text(i++, track.album_sort_key);
            bind_text(i++, track.album_hash);
            bind_or_null(i++, track.cuesheet_id);
            sqlite3_bind_int64(stmt, i++, now);
            sqlite3_bind_int64(stmt, i++, static_cast<int64_t>(track.file_device));
            sqlite3_bind_int64(stmt, i++, static_cast<int64_t>(track.file_inode));
            bind_text(i++, track.container_path);
            const int rc2 = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc2 != SQLITE_DONE) {
                return err(ErrorCode::QueryFailed, "Could not update track.",
                           sqlite3_errmsg(db));
            }
        } else if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not insert track.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Result<std::vector<TrackRecord>> Database::list_tracks(int64_t since, int64_t limit) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,container_path,filename,file_size,duration_ms,"
        "title,artist,album,albumartist,genre,year,tracknumber,discnumber,"
        "comment,bpm,composer,grouping,music_key,"
        "rg_track_gain,rg_track_peak,rg_album_gain,rg_album_peak,"
        "bitrate_kbps,sample_rate,bit_depth,channels,codec,container,is_lossless,"
        "artwork_id,has_artwork,lyrics_id,has_lyrics,"
        "source_id,source_path,"
        "rating,is_loved,is_blacklisted,play_count,skip_count,last_played_at,"
        "artist_sort_key,album_sort_key,album_hash,cuesheet_id,"
        "added_at,updated_at,file_device,file_inode "
        "FROM tracks WHERE deleted_at IS NULL AND id > ? ORDER BY id LIMIT ?;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare track list.", conn_.error_message());
    }
    sqlite3_bind_int64(stmt, 1, since);
    sqlite3_bind_int64(stmt, 2, limit);
    std::vector<TrackRecord> tracks;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tracks.push_back(read_track_row(stmt));
    }
    sqlite3_finalize(stmt);
    return tracks;
}

Result<TrackRecord> Database::get_track_by_path(std::string_view path) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,container_path,filename,file_size,duration_ms,"
        "title,artist,album,albumartist,genre,year,tracknumber,discnumber,"
        "comment,bpm,composer,grouping,music_key,"
        "rg_track_gain,rg_track_peak,rg_album_gain,rg_album_peak,"
        "bitrate_kbps,sample_rate,bit_depth,channels,codec,container,is_lossless,"
        "artwork_id,has_artwork,lyrics_id,has_lyrics,"
        "source_id,source_path,"
        "rating,is_loved,is_blacklisted,play_count,skip_count,last_played_at,"
        "artist_sort_key,album_sort_key,album_hash,cuesheet_id,"
        "added_at,updated_at,file_device,file_inode "
        "FROM tracks WHERE container_path=? AND deleted_at IS NULL;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare track lookup.", conn_.error_message());
    }
    sqlite3_bind_text(stmt, 1, path.data(),
                      static_cast<int>(path.size()), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        TrackRecord t = read_track_row(stmt);
        sqlite3_finalize(stmt);
        return t;
    }
    sqlite3_finalize(stmt);
    return err(ErrorCode::FileNotFound, "Track not found.");
}

Result<std::vector<std::string>> Database::get_all_paths() const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(conn_,
                           "SELECT container_path FROM tracks WHERE deleted_at IS NULL;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare paths query.", conn_.error_message());
    }
    std::vector<std::string> paths;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* p = sqlite3_column_text(stmt, 0);
        if (p) paths.emplace_back(reinterpret_cast<const char*>(p));
    }
    sqlite3_finalize(stmt);
    return paths;
}

Status Database::mark_paths_gone(const std::vector<std::string>& paths) {
    if (paths.empty()) return ok();
    return write([&](sqlite3* db) -> Status {
        const char* sql =
            "UPDATE tracks SET deleted_at=? WHERE container_path=? AND deleted_at IS NULL;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare mark-gone.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        for (const auto& p : paths) {
            sqlite3_bind_int64(stmt, 1, now);
            sqlite3_bind_text(stmt, 2, p.data(),
                             static_cast<int>(p.size()), SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        return ok();
    });
}

// ===========================================================================
//  Playlists
// ===========================================================================

Status Database::upsert_playlist(const PlaylistRecord& playlist) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO playlists(uuid,name,description,kind,rule_json,auto_refresh,"
            "sort_order,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(uuid) DO UPDATE SET "
            "name=excluded.name,description=excluded.description,"
            "rule_json=excluded.rule_json,auto_refresh=excluded.auto_refresh,"
            "sort_order=excluded.sort_order,updated_at=excluded.updated_at;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare playlist upsert.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_bind_text(stmt, 1, playlist.uuid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, playlist.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, playlist.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, playlist.kind);
        if (playlist.rule_json.empty())
            sqlite3_bind_null(stmt, 5);
        else
            sqlite3_bind_text(stmt, 5, playlist.rule_json.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, playlist.auto_refresh ? 1 : 0);
        sqlite3_bind_int64(stmt, 7, playlist.sort_order);
        sqlite3_bind_int64(stmt, 8, playlist.created_at > 0 ? playlist.created_at : now);
        sqlite3_bind_int64(stmt, 9, now);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not upsert playlist.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Result<std::vector<PlaylistRecord>> Database::list_playlists() const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,uuid,name,description,kind,rule_json,auto_refresh,sort_order,"
        "created_at,updated_at FROM playlists ORDER BY sort_order ASC, id ASC;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare playlist list.",
                   conn_.error_message());
    }
    std::vector<PlaylistRecord> playlists;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlaylistRecord p;
        p.id = sqlite3_column_int64(stmt, 0);
        const auto* uuid = sqlite3_column_text(stmt, 1);
        const auto* name = sqlite3_column_text(stmt, 2);
        const auto* desc = sqlite3_column_text(stmt, 3);
        const auto* rj = sqlite3_column_text(stmt, 5);
        p.uuid = uuid ? reinterpret_cast<const char*>(uuid) : "";
        p.name = name ? reinterpret_cast<const char*>(name) : "";
        p.description = desc ? reinterpret_cast<const char*>(desc) : "";
        p.kind = sqlite3_column_int(stmt, 4);
        p.rule_json = rj ? reinterpret_cast<const char*>(rj) : "";
        p.auto_refresh = sqlite3_column_int(stmt, 6) != 0;
        p.sort_order = sqlite3_column_int64(stmt, 7);
        p.created_at = sqlite3_column_int64(stmt, 8);
        p.updated_at = sqlite3_column_int64(stmt, 9);
        playlists.push_back(std::move(p));
    }
    sqlite3_finalize(stmt);
    return playlists;
}

Status Database::replace_playlist_items(int64_t playlist_id,
                                      const std::vector<int64_t>& track_ids) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                               "DELETE FROM playlist_items WHERE playlist_id=?;",
                               -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare item delete.",
                       sqlite3_errmsg(db));
        }
        sqlite3_bind_int64(stmt, 1, playlist_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (sqlite3_prepare_v2(
                db,
                "INSERT INTO playlist_items(playlist_id,position,track_id,added_at) "
                "VALUES(?,?,?,?);",
                -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare item insert.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        for (std::size_t i = 0; i < track_ids.size(); ++i) {
            sqlite3_bind_int64(stmt, 1, playlist_id);
            sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(i));
            sqlite3_bind_int64(stmt, 3, track_ids[i]);
            sqlite3_bind_int64(stmt, 4, now);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        return ok();
    });
}

Result<std::vector<PlaylistItemRecord>> Database::list_playlist_items(
    int64_t playlist_id) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT playlist_id,position,track_id,added_at FROM playlist_items "
        "WHERE playlist_id=? ORDER BY position ASC;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare playlist items query.",
                   conn_.error_message());
    }
    sqlite3_bind_int64(stmt, 1, playlist_id);
    std::vector<PlaylistItemRecord> items;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlaylistItemRecord r;
        r.playlist_id = sqlite3_column_int64(stmt, 0);
        r.position = sqlite3_column_int64(stmt, 1);
        r.track_id = sqlite3_column_int64(stmt, 2);
        r.added_at = sqlite3_column_int64(stmt, 3);
        items.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return items;
}

// ===========================================================================
//  Custom tags
// ===========================================================================

Status Database::set_custom_tag(int64_t track_id,
                                std::string_view key,
                                std::string_view value) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO track_custom_tags(track_id,key,value,updated_at) "
            "VALUES(?,?,?,?) "
            "ON CONFLICT(track_id,key,value) DO UPDATE SET updated_at=excluded.updated_at;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare custom tag set.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_bind_int64(stmt, 1, track_id);
        sqlite3_bind_text(stmt, 2, key.data(),
                         static_cast<int>(key.size()), SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, value.data(),
                         static_cast<int>(value.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, now);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not set custom tag.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Result<std::vector<CustomTagRecord>> Database::get_custom_tags(int64_t track_id) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT track_id,key,value,updated_at FROM track_custom_tags "
        "WHERE track_id=? ORDER BY key, value;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare custom tags query.",
                   conn_.error_message());
    }
    sqlite3_bind_int64(stmt, 1, track_id);
    std::vector<CustomTagRecord> tags;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CustomTagRecord r;
        r.track_id = sqlite3_column_int64(stmt, 0);
        const auto* k = sqlite3_column_text(stmt, 1);
        const auto* v = sqlite3_column_text(stmt, 2);
        r.key = k ? reinterpret_cast<const char*>(k) : "";
        r.value = v ? reinterpret_cast<const char*>(v) : "";
        r.updated_at = sqlite3_column_int64(stmt, 3);
        tags.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    return tags;
}

// ===========================================================================
//  Cue sheets  (REQ-LIB-040 .. REQ-LIB-044)
// ===========================================================================

Status Database::upsert_cuesheet(const CuesheetRecord& cue) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "INSERT INTO cuesheets(id,file_path,file_device,file_inode,performer,"
            "title,file_ref,file_is_flac,catalog,created_at) VALUES(?,?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(id) DO UPDATE SET "
            "file_path=excluded.file_path,file_device=excluded.file_device,"
            "file_inode=excluded.file_inode,performer=excluded.performer,"
            "title=excluded.title,file_ref=excluded.file_ref,"
            "file_is_flac=excluded.file_is_flac,catalog=excluded.catalog;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare cuesheet upsert.",
                       sqlite3_errmsg(db));
        }
        const auto now = static_cast<int64_t>(std::time(nullptr));
        sqlite3_bind_text(stmt, 1, cue.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, cue.file_path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(cue.file_device));
        sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(cue.file_inode));
        sqlite3_bind_text(stmt, 5, cue.performer.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, cue.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, cue.file_ref.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, cue.file_is_flac ? 1 : 0);
        sqlite3_bind_text(stmt, 9, cue.catalog.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 10, cue.created_at > 0 ? cue.created_at : now);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed, "Could not upsert cuesheet.",
                       sqlite3_errmsg(db));
        }
        return ok();
    });
}

Status Database::replace_cuetracks(const std::string& cuesheet_id,
                                  const std::vector<CueTrackRecord>& tracks) {
    return write([&](sqlite3* db) -> Status {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM cuetracks WHERE cuesheet_id=?;", -1,
                               &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare cuetrack delete.",
                       sqlite3_errmsg(db));
        }
        sqlite3_bind_text(stmt, 1, cuesheet_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (sqlite3_prepare_v2(
                db,
                "INSERT INTO cuetracks(cuesheet_id,position,title,performer,"
                "start_offset,end_offset,isrc,flags) VALUES(?,?,?,?,?,?,?,?);",
                -1, &stmt, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed, "Could not prepare cuetrack insert.",
                       sqlite3_errmsg(db));
        }
        for (const auto& ct : tracks) {
            sqlite3_bind_text(stmt, 1, cuesheet_id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 2, ct.position);
            sqlite3_bind_text(stmt, 3, ct.title.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 4, ct.performer.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 5, ct.start_offset);
            sqlite3_bind_int64(stmt, 6, ct.end_offset);
            sqlite3_bind_text(stmt, 7, ct.isrc.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 8, ct.flags.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);
        return ok();
    });
}

Result<std::vector<CueTrackRecord>> Database::get_cuetracks(
    const std::string& cuesheet_id) const {
    std::shared_lock lock{mutex_};
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id,cuesheet_id,position,title,performer,start_offset,end_offset,"
        "isrc,flags FROM cuetracks WHERE cuesheet_id=? ORDER BY position;";
    if (sqlite3_prepare_v2(conn_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed, "Could not prepare cuetracks query.",
                   conn_.error_message());
    }
    sqlite3_bind_text(stmt, 1, cuesheet_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<CueTrackRecord> tracks;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CueTrackRecord ct;
        ct.id = sqlite3_column_int64(stmt, 0);
        ct.cuesheet_id = cuesheet_id;
        ct.position = sqlite3_column_int64(stmt, 2);
        const auto* t = sqlite3_column_text(stmt, 3);
        const auto* p = sqlite3_column_text(stmt, 4);
        const auto* isrc = sqlite3_column_text(stmt, 7);
        const auto* f = sqlite3_column_text(stmt, 8);
        ct.title = t ? reinterpret_cast<const char*>(t) : "";
        ct.performer = p ? reinterpret_cast<const char*>(p) : "";
        ct.start_offset = sqlite3_column_int64(stmt, 5);
        ct.end_offset = sqlite3_column_int64(stmt, 6);
        ct.isrc = isrc ? reinterpret_cast<const char*>(isrc) : "";
        ct.flags = f ? reinterpret_cast<const char*>(f) : "";
        tracks.push_back(std::move(ct));
    }
    sqlite3_finalize(stmt);
    return tracks;
}

}  // namespace arrow::library
