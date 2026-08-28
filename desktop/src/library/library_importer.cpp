// SPDX-License-Identifier: MPL-2.0
#include "library/library_importer.hpp"

#include "library/library_database.hpp"

namespace arrow::library {

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
    // REQ-PLS-014: refresh on library change. The empty smart-playlist list
    // is a no-op (evaluate_all_smart_playlists returns 0 immediately), and
    // the cost grows linearly with the number of saved rules — never with
    // the number of tracks, because the SQL the engine runs is a single
    // prepared INSERT...SELECT per playlist.
    if (auto* db = dynamic_cast<LibraryDatabase*>(&library); db != nullptr) {
        if (auto eval = db->evaluate_all_smart_playlists(); !eval) {
            return Status{std::move(eval).error()};
        }
    }
    return ok();
}

}  // namespace arrow::library
