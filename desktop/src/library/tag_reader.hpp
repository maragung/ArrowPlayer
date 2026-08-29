// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Tag reader
// Spec: §6.6 (TagLib), §9.4, REQ-LIB-025 .. REQ-LIB-044
//
// Reads audio metadata using TagLib and computes:
//   - All tags per REQ-LIB-025-026
//   - Multi-valued artist/genre parsing per REQ-LIB-028
//   - Sort keys (NFKD + diacritic-strip) per REQ-LIB-029
//   - Album identity hash per REQ-LIB-031
//   - Cue sheet parsing per REQ-LIB-040-044
//
// All string normalization is locale-independent per the shared-spec contract.
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "library/database.hpp"

namespace arrow::library {

// ===========================================================================
//  Tag reader
// ===========================================================================

/// Configuration for the tag reader.
struct TagReaderConfig final {
    /// Parse audio properties (bitrate, sample rate, channels, duration).
    bool read_audio_properties{true};
    /// Parse tags. Set to false to skip tag reading (only read file stats).
    bool parse_tags{true};
    /// Maximum size for embedded artwork before rejecting (bytes).
    std::size_t max_artwork_bytes{16 * 1024 * 1024};  // 16 MB
};

/// Reads audio file metadata using TagLib.
///
/// Thread-safety: a TagReader is not thread-safe; create one per thread or
/// protect concurrent calls with a mutex. TagLib itself is not thread-safe for
/// the same file handle, but different files can be read concurrently.
class TagReader final {
  public:
    /// Construct with default configuration.
    TagReader();

    /// Construct with explicit configuration.
    explicit TagReader(const TagReaderConfig& config);

    ~TagReader();

    TagReader(const TagReader&) = delete;
    TagReader& operator=(const TagReader&) = delete;
    TagReader(TagReader&&) = default;
    TagReader& operator=(TagReader&&) = default;

    // -----------------------------------------------------------------------
    //  Reading tags
    // -----------------------------------------------------------------------

    /// Read all metadata from an audio file. Returns a TrackRecord with all
    /// fields populated from the file's tags and audio properties.
    ///
    /// Throws no exceptions on I/O or parsing errors; returns an Error instead.
    ///
    /// Errors:
    ///   - InvalidArgument: path is empty
    ///   - FileNotFound: file does not exist
    ///   - UnsupportedFormat: TagLib cannot read this format
    [[nodiscard]] Result<TrackRecord> read(const std::filesystem::path& path) const;

    /// Read audio metadata and attach a cue sheet reference.
    [[nodiscard]] Result<TrackRecord> read_with_cuesheet(
        const std::filesystem::path& media_path,
        const std::filesystem::path& cue_path) const;

    // -----------------------------------------------------------------------
    //  Cue sheets  (REQ-LIB-040 .. REQ-LIB-044)
    // -----------------------------------------------------------------------

    /// Parse a .cue file and return a CuesheetRecord with all CueTrackRecords.
    /// The cuesheet ID is computed as the SHA-256 of the raw file content.
    [[nodiscard]] Result<CuesheetRecord> parse_cuesheet(
        const std::filesystem::path& path) const;

    // -----------------------------------------------------------------------
    //  Custom tags  (REQ-LIB-033)
    // -----------------------------------------------------------------------

    /// Extract all custom/user-defined tags from a file. Known tag names are
    /// excluded; everything else is treated as a custom tag.
    [[nodiscard]] std::vector<CustomTagRecord> extract_custom_tags(
        const std::filesystem::path& path,
        int64_t track_id) const;

    // -----------------------------------------------------------------------
    //  Normalization helpers
    // -----------------------------------------------------------------------

    /// Compute the sort key for an artist or album name.
    /// The sort key is: NFKD normalize → strip diacritics → strip leading
    /// articles → lowercase. This is locale-independent.
    [[nodiscard]] Result<std::string> sort_key_for(std::string_view artist,
                                                   std::string_view album = {}) const;

    /// Compute the album identity hash from album + albumartist.
    [[nodiscard]] Result<std::string> album_hash_for(std::string_view album,
                                                     std::string_view albumartist) const;

    /// Split a multi-valued artist string on '/', ';', ',' separators.
    [[nodiscard]] std::vector<std::string> split_artists(const std::string& joined) const;

    /// Split a multi-valued genre string on '/', ';', ',' separators.
    [[nodiscard]] std::vector<std::string> split_genres(const std::string& joined) const;

  private:
    std::unique_ptr<class TagReaderImpl> impl_;
};

}  // namespace arrow::library
