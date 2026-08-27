// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <filesystem>

#include "ports/library_port.hpp"

namespace eclipse::library {

class ITagReader {
  public:
    virtual ~ITagReader() = default;
    [[nodiscard]] virtual Result<Track> read(const std::filesystem::path& path) const = 0;
};

}  // namespace eclipse::library
