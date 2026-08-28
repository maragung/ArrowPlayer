// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "ports/library_port.hpp"
#include "ports/tag_port.hpp"

namespace arrow::library {

class LibraryImporter final {
  public:
    [[nodiscard]] static Status import_files(const ScanRequest& request,
                                             const ILibraryScanner& scanner,
                                             const ITagReader& tags,
                                             ILibrary& library);
};

}  // namespace arrow::library
