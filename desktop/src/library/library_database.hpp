// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ports/library_port.hpp"

struct sqlite3;

namespace eclipse::library {

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

  private:
    [[nodiscard]] Status execute(std::string_view sql) const;
    sqlite3* db_{nullptr};
};

}  // namespace eclipse::library
