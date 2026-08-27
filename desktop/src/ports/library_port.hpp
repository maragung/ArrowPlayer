// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace eclipse::library {

struct Track final {
    std::string path;
    std::string title;
    std::string artist;
    std::int64_t duration_ms{0};
};

struct ScanRequest final {
    std::filesystem::path root;
    std::size_t max_files{100000};
};

class ILibrary {
  public:
    virtual ~ILibrary() = default;
    [[nodiscard]] virtual Status insert_track(const Track& track) = 0;
    [[nodiscard]] virtual Status upsert_track(const Track& track) = 0;
    [[nodiscard]] virtual Result<std::vector<Track>> list_tracks() const = 0;
    [[nodiscard]] virtual Status remove_track(std::string_view path) = 0;
};

class ILibraryScanner {
  public:
    virtual ~ILibraryScanner() = default;
    [[nodiscard]] virtual Result<std::vector<std::filesystem::path>> scan(
        const ScanRequest& request) const = 0;
};

}  // namespace eclipse::library
