// SPDX-License-Identifier: MPL-2.0
// Track context — adapts a Track to the EFS TrackView interface.
//
// Spec §10.4: the set of fields EFS can access.  Every field maps from
// the canonical track model to the evaluator's key-value interface.
// Absent/unset fields return std::nullopt; the empty string is a value
// (REQ-EFS-004).  Numeric fields are stored as strings in their canonical
// display form so the evaluator never needs to know column types.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/format/evaluator.hpp"

namespace arrow::efs {

// ---------------------------------------------------------------------------
//  Track — the canonical in-memory track model.
//
//  All fields are std::optional so an unset field is clearly distinct from an
//  empty-string value.  String fields use std::string (empty == value) except
//  where the source model uses std::nullopt for "not loaded yet".  Boolean
//  fields default to false (the absent value for a bool is false, not
//  nullopt — this matches the tag-data semantics and the conformance
//  fixtures).
// ---------------------------------------------------------------------------

struct Track final : public TrackView {
    // Metadata
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album_artist;
    std::optional<std::string> album;
    std::optional<std::string> genre;
    std::optional<std::string> composer;
    std::optional<std::string> comment;
    std::optional<std::string> grouping;

    // Numeric metadata (canonical display form as string)
    std::optional<std::string> year;          // "2000"
    std::optional<std::string> date;          // "2000-10-02"
    std::optional<std::string> track_number;   // "8"
    std::optional<std::string> track_total;    // "12"
    std::optional<std::string> disc_number;    // "1"
    std::optional<std::string> disc_total;    // "2"

    // Technical
    std::optional<std::string> duration_ms;   // "500" (raw milliseconds)
    std::optional<std::string> bitrate_kbps;  // "1002"
    std::optional<std::string> sample_rate;    // "44100"
    std::optional<std::string> bit_depth;     // "16"
    std::optional<std::string> channels;      // "2"
    std::optional<std::string> codec;         // "flac"
    std::optional<std::string> container;     // "flac"
    std::optional<bool>        is_lossless;    // true/false → "1"/"0"

    // Library state
    std::optional<std::string> path;         // absolute file path
    std::optional<std::string> filename;      // basename
    std::optional<std::string> file_ext;     // extension including dot
    std::optional<std::string> file_size;    // bytes as string

    std::optional<std::string> rating;        // 0..100 as string
    std::optional<std::string> play_count;   // as string
    std::optional<std::string> skip_count;
    std::optional<std::string> last_played_at; // ISO-8601 timestamp
    std::optional<std::string> added_at;       // ISO-8601 timestamp
    std::optional<std::string> bpm;
    std::optional<std::string> music_key;
    std::optional<std::string> rg_track_gain;

    // Flags
    bool loved{false};                        // → "1"/"0"
    bool missing{false};                     // → "1"/"0"
    bool has_artwork{false};
    bool has_lyrics{false};

    // Playback state (runtime; not persisted)
    std::optional<std::string> playing_state; // "playing"/"paused"/"stopped"
    std::optional<std::string> position_ms;   // playback position in ms
    std::optional<std::string> queue_index;
    std::optional<std::string> queue_total;
    std::optional<std::string> list_index;
    std::optional<std::string> list_total;

    /// "Now" for $age.  Returns the unix epoch by default; tests override.
    std::int64_t now_unix_{0};

    [[nodiscard]] std::int64_t now_unix() const noexcept override {
        return now_unix_;
    }

    // TrackView implementation ------------------------------------------------

    [[nodiscard]] std::optional<std::string> field(
        std::string_view name) const noexcept override;

    [[nodiscard]] std::optional<std::vector<std::string>> multi_field(
        std::string_view name) const noexcept override;
};

}  // namespace arrow::efs
