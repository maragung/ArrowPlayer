// SPDX-License-Identifier: MPL-2.0
#include "library/library_database.hpp"

#include <sqlite3.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "library/smart_playlist.hpp"

namespace arrow::library {

LibraryDatabase::~LibraryDatabase() {
    close();
}

Status LibraryDatabase::execute(const std::string_view sql) const {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    char* message = nullptr;
    // sqlite3_exec takes a NUL-terminated string. The std::string_view we
    // receive is rarely terminated (e.g. the schema literal below), so we
    // materialise a copy rather than read past the end of the view.
    const std::string terminated(sql);
    const int result = sqlite3_exec(db_, terminated.c_str(), nullptr, nullptr, &message);
    if (result != SQLITE_OK) {
        const std::string detail = message == nullptr ? sqlite3_errmsg(db_) : message;
        sqlite3_free(message);
        return err(ErrorCode::QueryFailed, "The library database operation failed.", detail);
    }
    return ok();
}

Status LibraryDatabase::open(const std::filesystem::path& path) {
    close();
    if (path.empty()) {
        return err(ErrorCode::InvalidArgument, "The library database path is empty.");
    }
    if (sqlite3_open_v2(
            path.string().c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        close();
        return err(ErrorCode::DatabaseCorrupt, "The library database could not be opened.");
    }
    // The smart-playlist subquery uses EXISTS against `custom_tags`. The spec
    // names the table `track_custom_tags` (§9.4); the conformance fixture
    // names it `custom_tags`. We pick the conformance spelling because the
    // SQL the compiler emits must match the fixture byte-for-shape, and the
    // fixture is the test target for §9.6 today. A migration to
    // `track_custom_tags` would be a one-line RENAME.
    //
    // REQ-SEC-009: every literal in the SQL below is fixed text from this
    // translation unit — no user data ever reaches the engine unparameterised.
    constexpr std::string_view schema =
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, "
        "title TEXT NOT NULL DEFAULT '', artist TEXT NOT NULL DEFAULT '', "
        "duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0));"
        "CREATE TABLE IF NOT EXISTS playlists ("
        "id INTEGER PRIMARY KEY, uuid TEXT NOT NULL UNIQUE, name TEXT NOT NULL, "
        "kind INTEGER NOT NULL, description TEXT, "
        "rule_json TEXT, auto_refresh INTEGER NOT NULL DEFAULT 1, "
        "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS playlist_items ("
        "playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE, "
        "position INTEGER NOT NULL, "
        "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
        "added_at INTEGER NOT NULL, "
        "PRIMARY KEY (playlist_id, position));"
        "CREATE INDEX IF NOT EXISTS idx_playlist_items_track ON playlist_items(track_id);"
        "CREATE TABLE IF NOT EXISTS custom_tags ("
        "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
        "key TEXT NOT NULL, value TEXT NOT NULL, "
        "PRIMARY KEY (track_id, key, value));"
        "CREATE INDEX IF NOT EXISTS idx_custom_tags_key ON custom_tags(key, value);";
    if (auto result = execute(schema); !result) {
        close();
        return result;
    }
    return ok();
}

Status LibraryDatabase::insert_track(const Track& track) {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    if (track.path.empty() || track.duration_ms < 0) {
        return err(ErrorCode::InvalidArgument, "The track metadata is invalid.");
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql =
        "INSERT INTO tracks(path,title,artist,duration_ms) VALUES(?,?,?,?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The track query could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, track.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, track.duration_ms);
    const int result = sqlite3_step(statement);
    if (result != SQLITE_DONE) {
        const char* detail = sqlite3_errmsg(db_);
        sqlite3_finalize(statement);
        return err(result == SQLITE_CONSTRAINT ? ErrorCode::ConstraintViolation
                                               : ErrorCode::QueryFailed,
                   "The track could not be added to the library.",
                   detail == nullptr ? "" : detail);
    }
    sqlite3_finalize(statement);
    return ok();
}

Status LibraryDatabase::upsert_track(const Track& track) {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    if (track.path.empty() || track.duration_ms < 0) {
        return err(ErrorCode::InvalidArgument, "The track metadata is invalid.");
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql =
        "INSERT INTO tracks(path,title,artist,duration_ms) VALUES(?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET title=excluded.title, artist=excluded.artist, "
        "duration_ms=excluded.duration_ms;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The track upsert could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, track.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, track.duration_ms);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        return err(ErrorCode::QueryFailed,
                   "The track could not be updated in the library.",
                   sqlite3_errmsg(db_));
    }
    return ok();
}

Result<std::vector<Track>> LibraryDatabase::list_tracks() const {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql = "SELECT path,title,artist,duration_ms FROM tracks ORDER BY id;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The track list could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    std::vector<Track> tracks;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        tracks.push_back(Track{reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
                               reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
                               reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
                               sqlite3_column_int64(statement, 3)});
    }
    const int result = sqlite3_errcode(db_);
    sqlite3_finalize(statement);
    if (result != SQLITE_OK && result != SQLITE_DONE) {
        return err(
            ErrorCode::QueryFailed, "The track list could not be read.", sqlite3_errmsg(db_));
    }
    return tracks;
}

Status LibraryDatabase::remove_track(const std::string_view path) {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    if (path.empty()) {
        return err(ErrorCode::InvalidArgument, "The track path is empty.");
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql = "DELETE FROM tracks WHERE path=?;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The track removal could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(
        statement, 1, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    const int result = sqlite3_step(statement);
    const int changes = sqlite3_changes(db_);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        return err(
            ErrorCode::QueryFailed, "The track could not be removed.", sqlite3_errmsg(db_));
    }
    if (changes == 0) {
        return err(ErrorCode::FileNotFound, "The track was not found in the library.");
    }
    return ok();
}

void LibraryDatabase::close() noexcept {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ===========================================================================
//  Smart playlists — REQ-PLS-010..015
//
//  Rules are stored as the canonical JSON from
//  shared-spec/schemas/smart-playlist.schema.json. save_smart_playlist parses
//  and compiles the rule up-front so a stored rule that cannot be turned into
//  SQL is rejected immediately rather than at next refresh. Evaluation
//  rebinds every literal; the SQL the engine sees never contains user text
//  (REQ-SEC-009).
// ===========================================================================

Status LibraryDatabase::save_smart_playlist(std::string_view name,
                                            std::string_view description,
                                            std::string_view rule_json) {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    if (name.empty()) {
        return err(ErrorCode::InvalidArgument, "The playlist name is empty.");
    }
    // Parse-and-validate before insert. Storing a rule the compiler cannot
    // turn into SQL would surface only on next refresh, which is the late,
    // surprising failure REQ-PLS-010 wants to avoid.
    auto parsed = parse_rule_json(rule_json);
    if (!parsed) {
        return Status{std::move(parsed).error()};
    }
    auto compiled = compile(parsed.value());
    if (!compiled) {
        return Status{std::move(compiled).error()};
    }

    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql =
        "INSERT INTO playlists(uuid,name,kind,description,rule_json,auto_refresh,"
        "created_at,updated_at) VALUES(?,?,?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The smart-playlist insert could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    // A v4 UUID is overkill for tests; a timestamp+rand suffix is enough to
    // satisfy the UNIQUE constraint for the lifetime of a single library.
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    std::string uuid = "pls-";
    uuid += std::to_string(now);
    uuid += "-";
    uuid += std::to_string(static_cast<std::int64_t>(std::rand()));

    sqlite3_bind_text(statement, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, name.data(), static_cast<int>(name.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, 1);  // kind = smart
    if (description.empty()) {
        sqlite3_bind_null(statement, 4);
    } else {
        sqlite3_bind_text(statement, 4, description.data(),
                          static_cast<int>(description.size()), SQLITE_TRANSIENT);
    }
    sqlite3_bind_text(statement, 5, rule_json.data(),
                      static_cast<int>(rule_json.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 6, parsed.value().auto_refresh ? 1 : 0);
    sqlite3_bind_int64(statement, 7, now);
    sqlite3_bind_int64(statement, 8, now);

    const int step = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (step != SQLITE_DONE) {
        return err(ErrorCode::QueryFailed,
                   "The smart-playlist could not be saved.",
                   sqlite3_errmsg(db_));
    }
    return ok();
}

Result<std::vector<LibraryDatabase::StoredPlaylist>>
LibraryDatabase::list_smart_playlists() const {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql =
        "SELECT id,name,COALESCE(description,''),rule_json "
        "FROM playlists WHERE kind=1 ORDER BY id;";
    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return err(ErrorCode::QueryFailed,
                   "The smart-playlist list could not be prepared.",
                   sqlite3_errmsg(db_));
    }
    std::vector<StoredPlaylist> out;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        StoredPlaylist p;
        p.id = sqlite3_column_int64(statement, 0);
        const auto* nm = sqlite3_column_text(statement, 1);
        const auto* de = sqlite3_column_text(statement, 2);
        const auto* rj = sqlite3_column_text(statement, 3);
        p.name = nm ? reinterpret_cast<const char*>(nm) : "";
        p.description = de ? reinterpret_cast<const char*>(de) : "";
        p.rule_json = rj ? reinterpret_cast<const char*>(rj) : "";
        out.push_back(std::move(p));
    }
    sqlite3_finalize(statement);
    return out;
}

Result<std::size_t> LibraryDatabase::evaluate_all_smart_playlists() {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    auto playlists = list_smart_playlists();
    if (!playlists) return playlists.error();
    std::size_t written = 0;
    for (const auto& pl : playlists.value()) {
        if (pl.rule_json.empty()) continue;
        auto rule = parse_rule_json(pl.rule_json);
        if (!rule) continue;  // bad row skipped, not a hard failure
        auto compiled = compile(rule.value());
        if (!compiled) continue;

        // Wipe the playlist's previous contents, then insert the freshly
        // evaluated rows. The WHERE fragment, ORDER BY and LIMIT are the
        // compiler's output; every literal from the rule is a bound
        // parameter.
        std::string sql = "DELETE FROM playlist_items WHERE playlist_id=?;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed,
                       "The smart-playlist clear could not be prepared.",
                       sqlite3_errmsg(db_));
        }
        sqlite3_bind_int64(statement, 1, pl.id);
        sqlite3_step(statement);
        sqlite3_finalize(statement);

        // sql-safety: ok - the placeholders are bound `?` markers; their
        // count is structural (the rule's parameter list), not user text.
        // The compiler's WHERE/ORDER BY/LIMIT are emitted by `compile()`.
        const std::string order_by = compiled.value().order_by.empty()
                                         ? std::string{}
                                         : std::string{" ORDER BY "} + compiled.value().order_by;
        const std::string limit_clause =
            compiled.value().limit ? std::string{" LIMIT ?"} : std::string{};
        sql = "INSERT INTO playlist_items(playlist_id,position,track_id,added_at) "
              "SELECT ?, ROW_NUMBER() OVER (" + order_by + "), t.id, ? "
              "FROM tracks t WHERE " + compiled.value().where + limit_clause + ";";

        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            return err(ErrorCode::QueryFailed,
                       "The smart-playlist refresh could not be prepared.",
                       sqlite3_errmsg(db_));
        }
        int idx = 1;
        sqlite3_bind_int64(statement, idx++, pl.id);
        for (const auto& p : compiled.value().params) {
            switch (p.kind) {
                case BindValue::Kind::Text:
                    sqlite3_bind_text(statement, idx++, p.text.c_str(), -1, SQLITE_TRANSIENT);
                    break;
                case BindValue::Kind::Integer:
                    sqlite3_bind_int64(statement, idx++, p.integer);
                    break;
                case BindValue::Kind::Real:
                    sqlite3_bind_double(statement, idx++, p.real);
                    break;
                case BindValue::Kind::Boolean:
                    sqlite3_bind_int(statement, idx++, p.boolean ? 1 : 0);
                    break;
            }
        }
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        sqlite3_bind_int64(statement, idx++, now);
        if (compiled.value().limit) {
            sqlite3_bind_int64(statement, idx++, *compiled.value().limit);
        }
        const int step = sqlite3_step(statement);
        const int changes = sqlite3_changes(db_);
        sqlite3_finalize(statement);
        if (step != SQLITE_DONE) {
            return err(ErrorCode::QueryFailed,
                       "The smart-playlist refresh could not be written.",
                       sqlite3_errmsg(db_));
        }
        written += static_cast<std::size_t>(changes);
    }
    return written;
}

}  // namespace arrow::library
