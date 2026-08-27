// SPDX-License-Identifier: MPL-2.0
#include "library/library_importer.hpp"

namespace eclipse::library {

Status LibraryImporter::import_files(const ScanRequest& request,
                                     const ILibraryScanner& scanner,
                                     const ITagReader& tags,
                                     ILibrary& library) {
    const auto files = scanner.scan(request);
    if (!files) {
        return std::move(files).error();
    }
    for (const auto& path : files.value()) {
        const auto metadata = tags.read(path);
        if (!metadata) return std::move(metadata).error();
        if (auto result = library.upsert_track(metadata.value()); !result) {
            return result;
        }
    }
    return ok();
}

}  // namespace eclipse::library
