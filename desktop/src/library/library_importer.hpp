// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "ports/library_port.hpp"
#include "ports/tag_port.hpp"

namespace arrow::library {

class LibraryImporter final {
  public:
    /// Scan, read tags, and upsert. After the tracks are in the library the
    /// importer re-evaluates every smart playlist (REQ-PLS-014) so the user
    /// opens a freshly imported folder to populated rules rather than stale
    /// ones. The smart-playlist evaluation lives on `LibraryDatabase` so the
    /// importer itself never has to know what the rule shape is.
    [[nodiscard]] static Status import_files(const ScanRequest& request,
                                             const ILibraryScanner& scanner,
                                             const ITagReader& tags,
                                             ILibrary& library);
};

}  // namespace arrow::library
