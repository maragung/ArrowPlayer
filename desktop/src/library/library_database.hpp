// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ports/library_port.hpp"

struct sqlite3;

namespace arrow::library {

class LibraryDatabase final : public ILibrary {
  public:
    LibraryDatabase() = default;
    ~LibraryDatabase();
    LibraryDatabase(const LibraryDatabase&) = delete;
    LibraryDatabase& operator=(const LibraryDatabase&) = delete;

    [[nodiscard]] Status open(const std::filesystem::path& path);
    [[nodiscard]] Status insert_track(const Track& track) override;
    [[nodiscard]] Status upsert_track(const Track& track) override;
    [[nodiscard]] Result<std::vector<Track>> list_tracks() const override;
    [[nodiscard]] Status remove_track(std::string_view path) override;
    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }

    // -----------------------------------------------------------------------
    //  Smart playlists — REQ-PLS-010..015. Rules are stored as the canonical
    //  JSON from shared-spec/schemas/smart-playlist.schema.json; evaluation
    //  binds every literal so a stored rule (or one imported with a skin)
    //  can never inject SQL (REQ-SEC-009).
    // -----------------------------------------------------------------------

    /// Persist a smart-playlist rule. `name` and `description` are the
    /// user-facing metadata; `rule_json` is the literal JSON document
    /// validated against the schema before insert.
    [[nodiscard]] Status save_smart_playlist(std::string_view name,
                                             std::string_view description,
                                             std::string_view rule_json);

    /// Every smart playlist currently in the library. Order is by id so
    /// refresh is stable across runs.
    struct StoredPlaylist final {
        std::int64_t id{0};
        std::string name;
        std::string description;
        std::string rule_json;
    };
    [[nodiscard]] Result<std::vector<StoredPlaylist>> list_smart_playlists() const;

    /// Re-evaluate every smart playlist and replace the contents of
    /// `playlist_items` for each one. Returns the number of (playlist,
    /// track) rows written. Used by the importer after a scan so the
    /// populated rows match the new library (REQ-PLS-014).
    [[nodiscard]] Result<std::size_t> evaluate_all_smart_playlists();

  private:
    [[nodiscard]] Status execute(std::string_view sql) const;

    sqlite3* db_{nullptr};
};

}  // namespace arrow::library
