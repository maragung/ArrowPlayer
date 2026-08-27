// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "ports/library_port.hpp"

namespace eclipse::library {

class FilesystemScanner final : public ILibraryScanner {
  public:
    [[nodiscard]] Result<std::vector<std::filesystem::path>> scan(
        const ScanRequest& request) const override;
};

}  // namespace eclipse::library
