// SPDX-License-Identifier: MPL-2.0
#include "library/library_database.hpp"

#include <sqlite3.h>

#include <vector>

namespace eclipse::library {

LibraryDatabase::~LibraryDatabase() { close(); }

Status LibraryDatabase::execute(const std::string_view sql) const {
    if (db_ == nullptr) {
        return err(ErrorCode::InvalidState, "The library database is not open.");
    }
    char* message = nullptr;
    const int result = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &message);
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
    if (sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        close();
        return err(ErrorCode::DatabaseCorrupt, "The library database could not be opened.");
    }
    constexpr std::string_view schema =
        "PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, title TEXT NOT NULL DEFAULT '', "
        "artist TEXT NOT NULL DEFAULT '', duration_ms INTEGER NOT NULL DEFAULT 0 CHECK(duration_ms >= 0));";
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
        return err(ErrorCode::QueryFailed, "The track query could not be prepared.", sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, track.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, track.duration_ms);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        return err(result == SQLITE_CONSTRAINT ? ErrorCode::ConstraintViolation : ErrorCode::QueryFailed,
                   "The track could not be added to the library.", sqlite3_errmsg(db_));
    }
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
        return err(ErrorCode::QueryFailed, "The track upsert could not be prepared.", sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, track.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 4, track.duration_ms);
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        return err(ErrorCode::QueryFailed, "The track could not be updated in the library.", sqlite3_errmsg(db_));
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
        return err(ErrorCode::QueryFailed, "The track list could not be prepared.", sqlite3_errmsg(db_));
    }
    std::vector<Track> tracks;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        tracks.push_back(Track{
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)),
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)),
            sqlite3_column_int64(statement, 3)});
    }
    const int result = sqlite3_errcode(db_);
    sqlite3_finalize(statement);
    if (result != SQLITE_OK && result != SQLITE_DONE) {
        return err(ErrorCode::QueryFailed, "The track list could not be read.", sqlite3_errmsg(db_));
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
        return err(ErrorCode::QueryFailed, "The track removal could not be prepared.", sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(statement, 1, path.data(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
    const int result = sqlite3_step(statement);
    const int changes = sqlite3_changes(db_);
    sqlite3_finalize(statement);
    if (result != SQLITE_DONE) {
        return err(ErrorCode::QueryFailed, "The track could not be removed.", sqlite3_errmsg(db_));
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

}  // namespace eclipse::library
