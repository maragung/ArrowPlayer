// SPDX-License-Identifier: MPL-2.0
#include "core/format/track_context.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arrow::efs {

std::optional<std::string> Track::field(std::string_view name) const noexcept {
    // Field name mapping: EFS name → Track member
    // EFS names follow the conformance fixture convention (no underscores)
    // Track members use underscores.

    // Metadata
    if (name == "title")        return title;
    if (name == "artist")       return artist;
    if (name == "albumartist")  return album_artist;
    if (name == "album")        return album;
    if (name == "genre")        return genre;
    if (name == "composer")     return composer;
    if (name == "comment")      return comment;
    if (name == "grouping")     return grouping;

    // Numeric metadata (canonical display form as string)
    if (name == "year")         return year;
    if (name == "date")         return date;
    if (name == "tracknumber")  return track_number;
    if (name == "tracktotal")   return track_total;
    if (name == "discnumber")   return disc_number;
    if (name == "disctotal")    return disc_total;

    // Technical
    if (name == "duration")     return duration_ms;
    if (name == "bitrate")      return bitrate_kbps;
    if (name == "samplerate")   return sample_rate;
    if (name == "bitdepth")     return bit_depth;
    if (name == "channels")     return channels;
    if (name == "codec")        return codec;
    if (name == "container")     return container;
    if (name == "islossless") {
        if (is_lossless.has_value()) return std::string{*is_lossless ? "1" : "0"};
        return std::nullopt;
    }

    // Library state
    if (name == "path")         return path;
    if (name == "filename")     return filename;
    if (name == "filesize")     return file_size;
    if (name == "rating")       return rating;
    if (name == "playcount")    return play_count;
    if (name == "skipcount")    return skip_count;
    if (name == "lastplayed")   return last_played_at;
    if (name == "added")       return added_at;
    if (name == "bpm")         return bpm;
    if (name == "key")          return music_key;
    if (name == "rgtrackgain") return rg_track_gain;
    if (name == "loved")        return std::string{loved ? "1" : "0"};
    if (name == "missing")      return std::string{missing ? "1" : "0"};
    if (name == "hasartwork")   return std::string{has_artwork ? "1" : "0"};
    if (name == "haslyrics")    return std::string{has_lyrics ? "1" : "0"};

    // Playback state (runtime)
    if (name == "playing_state") return playing_state;
    if (name == "position")     return position_ms;
    if (name == "queue_index")  return queue_index;
    if (name == "queue_total")  return queue_total;
    if (name == "list_index")   return list_index;
    if (name == "list_total")   return list_total;

    // Unknown field → absent (not an error; REQ-EFS-006)
    return std::nullopt;
}

std::optional<std::vector<std::string>> Track::multi_field(
    std::string_view name) const noexcept {
    // All fields are single-valued in this implementation.
    // Multi-valued artist/genre metadata is stored as a separator-joined string;
    // the library layer is responsible for splitting/joining.
    auto v = field(name);
    if (!v.has_value()) return std::nullopt;
    return std::vector<std::string>{std::move(*v)};
}

}  // namespace arrow::efs
