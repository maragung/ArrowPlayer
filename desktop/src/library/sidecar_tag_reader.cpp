// SPDX-License-Identifier: MPL-2.0
#include "library/sidecar_tag_reader.hpp"

#include <fstream>
#include <limits>
#include <string>

namespace arrow::library {

Result<Track> SidecarTagReader::read(const std::filesystem::path& path) const {
    if (path.empty()) {
        return err(ErrorCode::InvalidArgument, "The media path is empty.");
    }
    Track track{path.generic_string(), path.stem().string(), {}, 0};
    std::ifstream input(path.string() + ".arrow-tags");
    if (!input) return track;

    std::string line;
    std::size_t bytes = 0;
    while (std::getline(input, line)) {
        bytes += line.size();
        if (bytes > 64 * 1024) {
            return err(ErrorCode::InputTooLarge, "The metadata sidecar is too large.");
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "title")
            track.title = value;
        else if (key == "artist")
            track.artist = value;
        else if (key == "duration_ms") {
            try {
                const auto duration = std::stoll(value);
                if (duration < 0)
                    return err(ErrorCode::InvalidArgument, "The track duration is invalid.");
                track.duration_ms = duration;
            } catch (...) {
                return err(ErrorCode::MalformedHeader, "The track duration is invalid.");
            }
        }
    }
    return track;
}

}  // namespace arrow::library
