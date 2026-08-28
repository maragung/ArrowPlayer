// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "ports/tag_port.hpp"

namespace arrow::library {

class SidecarTagReader final : public ITagReader {
  public:
    [[nodiscard]] Result<Track> read(const std::filesystem::path& path) const override;
};

}  // namespace arrow::library
