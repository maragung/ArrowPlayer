// SPDX-License-Identifier: MPL-2.0
#include "library/filesystem_scanner.hpp"

#include <algorithm>
#include <array>
#include <system_error>

#include <cctype>

namespace eclipse::library {
namespace {

[[nodiscard]] bool supported_extension(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    std::string lower;
    lower.reserve(extension.size());
    for (const char character : extension) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lower == ".mp3" || lower == ".flac" || lower == ".ogg" || lower == ".opus" ||
           lower == ".wav" || lower == ".m4a" || lower == ".aac";
}

}  // namespace

Result<std::vector<std::filesystem::path>> FilesystemScanner::scan(const ScanRequest& request) const {
    if (request.root.empty() || request.max_files == 0) {
        return err(ErrorCode::InvalidArgument, "The library scan request is invalid.");
    }
    std::error_code status;
    if (!std::filesystem::is_directory(request.root, status) || status) {
        return err(status == std::errc::permission_denied ? ErrorCode::PermissionDenied
                                                           : ErrorCode::NotADirectory,
                   "The library root is not accessible.");
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(std::min<std::size_t>(request.max_files, 4096U));
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(request.root, options, status);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end && !status) {
        const auto& entry = *iterator;
        std::error_code entry_status;
        if (entry.is_regular_file(entry_status) && !entry_status && supported_extension(entry.path())) {
            paths.push_back(entry.path());
            if (paths.size() >= request.max_files) break;
        }
        iterator.increment(status);
    }
    if (status && status != std::errc::permission_denied) {
        return err(ErrorCode::IoError, "The library scan could not complete.");
    }
    std::sort(paths.begin(), paths.end());
    if (paths.size() > request.max_files) paths.resize(request.max_files);
    return paths;
}

}  // namespace eclipse::library
