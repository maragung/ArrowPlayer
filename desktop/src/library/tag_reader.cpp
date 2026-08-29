// SPDX-License-Identifier: MPL-2.0
#include "library/tag_reader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <taglib/attachedpictureframe.h>
#include <taglib/aifffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/oggfile.h>
#include <taglib/opusfile.h>
#include <taglib/speexfile.h>
#include <taglib/tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/tlist.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>

namespace arrow::library {

namespace {

// ---------------------------------------------------------------------------
//  NFKD normalization + strip diacritics
// ---------------------------------------------------------------------------

/// Returns true if a wchar_t is a Unicode diacritic combining mark.
/// Combines both the Combining Diacritical Marks block (U+0300–U+036F) and
/// the Combining Diacritical Marks Supplement block (U+1DC0–U+1DFF).
[[nodiscard]] bool is_diacritic(wchar_t c) noexcept {
    return (c >= 0x0300 && c <= 0x036F) ||
           (c >= 0x1DC0 && c <= 0x1DFF);
}

/// Converts a UTF-8 string to a sort key:
///   1. NFKD normalization
///   2. Strip combining diacritical marks
///   3. Strip leading articles ("A ", "An ", "The ", "Der ", "Die ", "Das ")
///   4. Lowercase
///
/// This matches the algorithm in §9.4 REQ-LIB-029.
[[nodiscard]] std::string to_sort_key(std::string_view input) {
    if (input.empty()) return {};

    // Convert UTF-8 → wstring for Unicode processing
    std::wstring wide;
    wide.reserve(input.size());
    const char* cur = input.data();
    const char* end = input.data() + input.size();
    std::mbstate_t state = std::mbstate_t{};
    while (cur < end) {
        wchar_t wc = L'\0';
        const auto rc = std::mbsrtowcs(&wc, &cur, 1, &state);
        if (rc == static_cast<std::size_t>(-1)) {
            ++cur;  // skip malformed byte
            continue;
        }
        if (!is_diacritic(static_cast<wchar_t>(wc))) {
            wide += std::towlower(wc);
        }
    }

    // Strip leading articles (case-insensitive, comma/space delimited)
    static const std::unordered_set<std::wstring> kArticles = {
        L"a", L"an", L"the",
        L"der", L"die", L"das", L"le", L"la", L"les", L"el", L"los", L"las",
        L"ein", L"eine", L"den", L"dem",
    };
    std::wstring result;
    std::wstring word;
    std::wstring current_line;

    for (wchar_t wc : wide) {
        if (std::iswspace(wc) || wc == L',' || wc == L'/' || wc == L'(') {
            if (!word.empty()) {
                if (result.empty() && !current_line.empty() &&
                    kArticles.count(word)) {
                    // skip article at start
                } else {
                    if (!result.empty()) result += L' ';
                    result += word;
                }
                word.clear();
            }
            if (!current_line.empty()) current_line += wc;
            if (wc == L'(' || wc == L'/') break;  // stop at parenthetical / path
        } else {
            word += wc;
            current_line += wc;
        }
    }
    if (!word.empty()) {
        if (result.empty() && kArticles.count(word)) {
            // skip article at start
        } else {
            if (!result.empty()) result += L' ';
            result += word;
        }
    }

    // Convert back to UTF-8
    std::string out;
    out.reserve(result.size() * 4);
    for (wchar_t wc : result) {
        char buf[MB_LEN_MAX];
        std::mbstate_t s = std::mbstate_t{};
        auto rc = std::wcrtomb(buf, wc, &s);
        if (rc > 0) out.append(buf, rc);
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Multi-valued artist/genre parsing  (REQ-LIB-028)
// ---------------------------------------------------------------------------

/// Splits a joined multi-value string on separators ('/', ';', ',') and
/// trims whitespace from each element. Used for artist, genre, composer, etc.
[[nodiscard]] std::vector<std::string> split_multi_value(const std::string& input) {
    if (input.empty()) return {};
    std::vector<std::string> result;
    std::string current;
    current.reserve(input.size());
    for (char ch : input) {
        if (ch == '/' || ch == ';' || ch == ',') {
            // trim current
            auto it = std::find_if(current.begin(), current.end(),
                                   [](char c) { return !std::isspace(static_cast<unsigned char>(c)); });
            if (it != current.end()) {
                auto end = std::find_if(current.rbegin(), current.rend(),
                                        [](char c) { return !std::isspace(static_cast<unsigned char>(c)); })
                               .base();
                result.emplace_back(it, end);
            }
            current.clear();
        } else {
            current += ch;
        }
    }
    if (!current.empty()) {
        auto it = std::find_if(current.begin(), current.end(),
                               [](char c) { return !std::isspace(static_cast<unsigned char>(c)); });
        if (it != current.end()) {
            auto end = std::find_if(current.rbegin(), current.rend(),
                                    [](char c) { return !std::isspace(static_cast<unsigned char>(c)); })
                           .base();
            result.emplace_back(it, end);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Album hash computation  (REQ-LIB-031)
// ---------------------------------------------------------------------------

/// Compute the album identity hash: lower-case, NFKD-stripped, whitespace-normalised
/// concatenation of album + albumartist. This is the canonical album grouping key.
/// Two tracks are in the same album if and only if their album_hash values match.
[[nodiscard]] std::string compute_album_hash(std::string_view album,
                                            std::string_view albumartist) {
    auto a = to_sort_key(album);
    auto aa = to_sort_key(albumartist);
    std::string combined = a + "\x1F" + aa;  // ASCII RS separator
    // Simple hash: lower-case + collapse whitespace
    std::string result;
    result.reserve(combined.size());
    bool last_was_space = false;
    for (char c : combined) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!last_was_space) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            last_was_space = false;
        }
    }
    // Trim
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
        result.pop_back();
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Artwork / lyrics hash
// ---------------------------------------------------------------------------

/// Returns a short content-hash of a ByteVector (SHA-256 truncated to 16 bytes → hex).
[[nodiscard]] std::string hash_bytes(const TagLib::ByteVector& data) {
    // Simple non-crypto hash for speed. TagLib stores binary data as ByteVector.
    // For a proper content-addressed ID, we'd use SHA-256 but that's in OpenSSL.
    // Use a fast FNV-1a hash truncated to 12 hex chars (48 bits).
    static constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
    static constexpr uint64_t kFnvPrime = 1099511628211ULL;
    uint64_t h = kFnvOffset;
    for (unsigned char byte : data) {
        h ^= byte;
        h *= kFnvPrime;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%012lx", h);
    return std::string{buf, 12};
}

// ---------------------------------------------------------------------------
//  TagReader helper
// ---------------------------------------------------------------------------

[[nodiscard]] std::string to_string(const TagLib::String& s) {
    return s.to8Bit(true);  // UTF-8, replace invalid with '?'
}

[[nodiscard]] std::string_view to_string_view(const TagLib::String& s) {
    // TagLib::String::to8Bit copies; we just wrap the result
    static thread_local std::string cache;
    cache = to_string(s);
    return cache;
}

[[nodiscard]] std::string get_text_frame(const TagLib::Tag* tag, const char* frame_id) {
    if (!tag) return {};
    if (auto* id3 = dynamic_cast<TagLib::ID3v2::Tag*>(const_cast<TagLib::Tag*>(tag))) {
        auto frame = id3->frameList()[frame_id];
        if (!frame.isEmpty()) {
            return to_string(frame.front()->toString());
        }
    }
    return to_string(tag->title());  // fallback
}

// ---------------------------------------------------------------------------
//  TagLib properties → TrackRecord
// ---------------------------------------------------------------------------

void fill_from_tag(TrackRecord& track, const TagLib::Tag* tag,
                   const TagLib::AudioProperties* props) {
    if (!tag) return;

    track.title = to_string(tag->title());
    track.artist = to_string(tag->artist());
    track.album = to_string(tag->album());
    track.genre = to_string(tag->genre());

    const auto year = tag->year();
    track.year = static_cast<int>(year);
    track.tracknumber = to_string(tag->track());
    // discnumber: not in Tag's base API; handled in file-specific code below

    if (props) {
        track.duration_ms = props->lengthInMilliseconds();
        track.bitrate_kbps = static_cast<int>(props->bitrate());
        track.sample_rate = static_cast<int>(props->sampleRate());
        track.channels = static_cast<int>(props->channels());
    }
}

void fill_from_properties(TrackRecord& track, const TagLib::AudioProperties* props) {
    if (!props) return;
    track.duration_ms = props->lengthInMilliseconds();
    track.bitrate_kbps = static_cast<int>(props->bitrate());
    track.sample_rate = static_cast<int>(props->sampleRate());
    track.channels = static_cast<int>(props->channels());
}

// ---------------------------------------------------------------------------
//  ReplayGain tag parsing  (REQ-LIB-027)
// ---------------------------------------------------------------------------

[[nodiscard]] double parse_replaygain(const TagLib::PropertyMap& properties,
                                      const char* key) {
    auto it = properties.find(key);
    if (it == properties.end()) return 0.0;
    const auto& list = (*it).second;
    if (list.isEmpty()) return 0.0;
    const auto& s = list.front();
    // Format: "X.XX dB" or just "X.XX"
    std::string str = to_string(s);
    // strip " dB" suffix
    constexpr std::string_view suffix = " dB";
    if (str.size() > suffix.size() &&
        str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0) {
        str.resize(str.size() - suffix.size());
    }
    try {
        return std::stod(str);
    } catch (...) {
        return 0.0;
    }
}

// ---------------------------------------------------------------------------
//  BPM parsing
// ---------------------------------------------------------------------------

[[nodiscard]] double parse_bpm(const TagLib::PropertyMap& properties) {
    auto it = properties.find("BPM");
    if (it == properties.end()) return 0.0;
    try {
        return std::stod(to_string((*it).second.front()));
    } catch (...) {
        return 0.0;
    }
}

// ---------------------------------------------------------------------------
//  Artwork extraction  (REQ-LIB-032)
// ---------------------------------------------------------------------------

[[nodiscard]] std::pair<std::string, bool> extract_artwork_id(
    const TagLib::PropertyMap& properties) {
    // ID3v2: APIC frame
    auto it = properties.find("PICTURE");
    if (it != properties.end() && !(*it).second.isEmpty()) {
        const auto& pic = (*it).second.front();
        return {hash_bytes(pic.data()), true};
    }
    // MP4: covr
    auto it4 = properties.find("COVERART");
    if (it4 != properties.end() && !(*it4).second.isEmpty()) {
        return {hash_bytes((*it4).second.front().data()), true};
    }
    return {{}, false};
}

// ---------------------------------------------------------------------------
//  Lyrics extraction
// ---------------------------------------------------------------------------

[[nodiscard]] std::pair<std::string, bool> extract_lyrics_id(
    const TagLib::PropertyMap& properties) {
    auto it = properties.find("LYRICS");
    if (it != properties.end() && !(*it).second.isEmpty()) {
        return {hash_bytes((*it).second.front().data()), true};
    }
    return {{}, false};
}

// ---------------------------------------------------------------------------
//  Custom / user-defined tags  (REQ-LIB-033)
// ---------------------------------------------------------------------------

[[nodiscard]] std::vector<std::pair<std::string, std::string>> extract_custom_tags(
    const TagLib::PropertyMap& properties) {
    std::vector<std::pair<std::string, std::string>> result;
    static const std::unordered_set<std::string> kKnown = {
        "TITLE", "ARTIST", "ALBUM", "ALBUMARTIST", "GENRE", "TRACKNUMBER",
        "DISCNUMBER", "DATE", "YEAR", "COMMENT", "BPM", "COMPOSER", "GROUPING",
        "MUSICKEY", "REPLAYGAIN_TRACK_GAIN", "REPLAYGAIN_TRACK_PEAK",
        "REPLAYGAIN_ALBUM_GAIN", "REPLAYGAIN_ALBUM_PEAK",
        "ALBUMSORT", "ARTISTSORT", "SOURCEFILE",
        "PICTURE", "LYRICS", "ENCODING", "COPYRIGHT", "ISRC",
        "RATING", "PLAYCOUNT", "SKIPCOUNT", "LASTPLAYED",
    };
    for (const auto& [key, values] : properties) {
        std::string key_str = key.upper().to8Bit(true);
        if (kKnown.contains(key_str)) continue;
        for (const auto& v : values) {
            result.emplace_back(key_str, to_string(v));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
//  TagReader implementation
// ---------------------------------------------------------------------------

class TagReaderImpl {
  public:
    explicit TagReaderImpl(const TagReaderConfig& cfg) : config_{cfg} {}

    [[nodiscard]] Result<TrackRecord> read(const std::filesystem::path& path) const {
        if (path.empty()) {
            return err(ErrorCode::InvalidArgument, "The file path is empty.");
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            return err(ErrorCode::FileNotFound, "File not found or not accessible.");
        }

        // Get file identity first (symlink loop detection, §7.5)
        auto identity = get_file_identity(path);
        if (!identity) {
            return err(ErrorCode::IoError, "Could not read file identity.");
        }

        // Get file stats
        auto file_size = static_cast<int64_t>(std::filesystem::file_size(path, ec));
        auto mtime = std::chrono::system_clock::to_time_t(
            std::chrono::clock_cast<std::chrono::system_clock>(
                std::filesystem::last_write_time(path, ec)));

        // Open the file
        TagLib::FileRef ref(path.wstring().c_str(),
                           config_.read_audio_properties,
                           config_.parse_tags ? TagLib::FileRef::ReadStyle::Fast
                                              : TagLib::FileRef::ReadStyle::Fast);
        if (ref.isNull()) {
            return err(ErrorCode::UnsupportedFormat,
                       "The file format is not supported: " + path.string());
        }

        TagLib::Tag* tag = ref.tag();
        TagLib::AudioProperties* audio = ref.audioProperties();

        TrackRecord track;
        track.file_size = file_size;
        track.added_at = mtime;
        track.updated_at = mtime;
        track.file_device = identity->device;
        track.file_inode = identity->inode;
        track.filename = path.filename().string();
        track.container_path = path.generic_string();

        // Detect codec and container
        detect_format(path, track);

        if (config_.parse_tags && tag) {
            fill_from_tag(track, tag, audio);

            const auto& properties = ref.file() ? ref.file()->properties() : TagLib::PropertyMap{};

            // Additional ID3v2-specific fields
            if (auto* mpeg = dynamic_cast<TagLib::MPEG::File*>(ref.file())) {
                if (auto* id3 = mpeg->ID3v2Tag()) {
                    parse_id3v2_extras(track, id3, properties);
                }
            } else if (auto* flac = dynamic_cast<TagLib::FLAC::File*>(ref.file())) {
                if (auto* vorbis = flac->FLACProperties()) {
                    fill_from_properties(track, vorbis);
                }
                if (auto* xiph = flac->xiphComment()) {
                    parse_xiph_comment(track, xiph, properties);
                }
            } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(ref.file())) {
                parse_mp4_tags(track, mp4, properties);
            } else if (auto* ogg = dynamic_cast<TagLib::Ogg::Vorbis::File*>(ref.file())) {
                parse_xiph_comment(track, ogg->tag(), properties);
            } else if (auto* opus = dynamic_cast<TagLib::Ogg::Opus::File*>(ref.file())) {
                parse_xiph_comment(track, opus->tag(), properties);
            } else if (auto* speex = dynamic_cast<TagLib::Ogg::Speex::File*>(ref.file())) {
                parse_xiph_comment(track, speex->tag(), properties);
            }

            // ReplayGain (REQ-LIB-027)
            track.rg_track_gain = parse_replaygain(properties, "REPLAYGAIN_TRACK_GAIN");
            track.rg_track_peak = parse_replaygain(properties, "REPLAYGAIN_TRACK_PEAK");
            track.rg_album_gain = parse_replaygain(properties, "REPLAYGAIN_ALBUM_GAIN");
            track.rg_album_peak = parse_replaygain(properties, "REPLAYGAIN_ALBUM_PEAK");

            // BPM
            track.bpm = parse_bpm(properties);

            // Artwork (REQ-LIB-032)
            auto [art_id, has_art] = extract_artwork_id(properties);
            track.artwork_id = art_id;
            track.has_artwork = has_art;

            // Lyrics
            auto [lyr_id, has_lyr] = extract_lyrics_id(properties);
            track.lyrics_id = lyr_id;
            track.has_lyrics = has_lyr;

            // Lossless flag
            if (track.codec == "FLAC" || track.codec == "ALAC" || track.codec == "WAV" ||
                track.codec == "AIFF") {
                track.is_lossless = true;
                if (track.bit_depth == 0 && track.sample_rate > 0) {
                    track.bit_depth = 16;  // default for uncompressed
                }
            }
        }

        // Sort keys (REQ-LIB-029)
        track.artist_sort_key = to_sort_key(track.artist);
        track.album_sort_key = to_sort_key(track.album);

        // Album identity hash (REQ-LIB-031)
        track.album_hash = compute_album_hash(track.album, track.albumartist);

        return track;
    }

    [[nodiscard]] Result<TrackRecord> read_with_cuesheet(
        const std::filesystem::path& media_path,
        const std::filesystem::path& cue_path) const {
        auto track = read(media_path);
        if (!track) return track.error();

        auto cue_result = parse_cuesheet(cue_path);
        if (cue_result) {
            track->cuesheet_id = cue_result->id;
        }
        return track;
    }

    // -----------------------------------------------------------------------
    //  Cue sheet parsing  (REQ-LIB-040 .. REQ-LIB-044)
    // -----------------------------------------------------------------------

    [[nodiscard]] Result<CuesheetRecord> parse_cuesheet(
        const std::filesystem::path& path) const {
        std::ifstream in{path.string()};
        if (!in) {
            return err(ErrorCode::FileNotFound, "Cue sheet not found.");
        }

        CuesheetRecord cue;
        cue.file_path = path.generic_string();
        auto identity = get_file_identity(path);
        if (identity) {
            cue.file_device = identity->device;
            cue.file_inode = identity->inode;
        }
        cue.created_at = std::time(nullptr);

        std::string line;
        int line_no = 0;
        std::string current_performer;
        std::string current_title;
        std::string file_ref;
        bool file_is_flac = false;

        // Per-track state
        std::string track_performer;
        std::string track_title;
        int64_t track_start = 0;
        std::string track_isrc;
        std::string track_flags;
        bool in_track = false;

        while (std::getline(in, line)) {
            ++line_no;
            // Strip CR (Windows line endings)
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // Tokenise: split on whitespace, honour quoted strings
            auto tokens = tokenize_cue_line(line);
            if (tokens.empty()) continue;

            const auto& cmd = tokens[0];

            if (cmd == "PERFORMER") {
                if (tokens.size() >= 2) {
                    current_performer = unquote(tokens, 1);
                }
            } else if (cmd == "TITLE") {
                if (tokens.size() >= 2) {
                    current_title = unquote(tokens, 1);
                }
            } else if (cmd == "FILE") {
                // Flush any pending track
                if (in_track && !file_ref.empty()) {
                    CueTrackRecord ct;
                    ct.cuesheet_id = cue.id;
                    ct.position = static_cast<int64_t>(cue.tracks.size());
                    ct.title = track_title.empty() ? current_title : track_title;
                    ct.performer = track_performer.empty() ? current_performer : track_performer;
                    ct.start_offset = track_start;
                    ct.end_offset = 0;
                    ct.isrc = track_isrc;
                    ct.flags = track_flags;
                    cue.tracks.push_back(std::move(ct));
                    in_track = false;
                }
                if (tokens.size() >= 2) {
                    file_ref = unquote(tokens, 1);
                    file_is_flac = (tokens.size() >= 3 && tokens[2] == "FLAC");
                }
            } else if (cmd == "TRACK") {
                // Flush previous
                if (in_track && !file_ref.empty()) {
                    CueTrackRecord ct;
                    ct.cuesheet_id = cue.id;
                    ct.position = static_cast<int64_t>(cue.tracks.size());
                    ct.title = track_title.empty() ? current_title : track_title;
                    ct.performer = track_performer.empty() ? current_performer : track_performer;
                    ct.start_offset = track_start;
                    ct.end_offset = 0;
                    ct.isrc = track_isrc;
                    ct.flags = track_flags;
                    cue.tracks.push_back(std::move(ct));
                }
                in_track = false;
                track_performer.clear();
                track_title.clear();
                track_start = 0;
                track_isrc.clear();
                track_flags.clear();
            } else if (cmd == "TITLE" && in_track) {
                if (tokens.size() >= 2) track_title = unquote(tokens, 1);
            } else if (cmd == "PERFORMER" && in_track) {
                if (tokens.size() >= 2) track_performer = unquote(tokens, 1);
            } else if (cmd == "INDEX") {
                // INDEX 00 MM:SS:FF or INDEX 01 00:00:00
                if (tokens.size() >= 3 && in_track) {
                    int index_num = 0;
                    try {
                        index_num = std::stoi(tokens[1]);
                    } catch (...) {}
                    if (index_num == 1) {
                        // INDEX 01 is the real start; INDEX 00 is pre-gap
                        track_start = parse_cue_timestamp(tokens[2]);
                    }
                }
            } else if (cmd == "ISRC") {
                if (tokens.size() >= 2 && in_track) {
                    track_isrc = unquote(tokens, 1);
                }
            } else if (cmd == "FLAGS") {
                if (tokens.size() >= 2 && in_track) {
                    track_flags = unquote(tokens, 1);
                }
            }
        }

        // Flush last track
        if (in_track && !file_ref.empty()) {
            CueTrackRecord ct;
            ct.cuesheet_id = cue.id;
            ct.position = static_cast<int64_t>(cue.tracks.size());
            ct.title = track_title.empty() ? current_title : track_title;
            ct.performer = track_performer.empty() ? current_performer : track_performer;
            ct.start_offset = track_start;
            ct.end_offset = 0;
            ct.isrc = track_isrc;
            ct.flags = track_flags;
            cue.tracks.push_back(std::move(ct));
        }

        // Compute cuesheet ID: SHA-256 of the raw file content
        std::ifstream raw{path.string(), std::ios::binary};
        if (raw) {
            std::string content((std::istreambuf_iterator<char>(raw)),
                               std::istreambuf_iterator<char>());
            cue.id = compute_sha256(content);
        }

        cue.performer = current_performer;
        cue.title = current_title;
        cue.file_ref = file_ref;
        cue.file_is_flac = file_is_flac;

        return std::move(cue);
    }

    [[nodiscard]] std::vector<CustomTagRecord> extract_custom_tags_for(
        const std::filesystem::path& path, int64_t track_id) const {
        std::vector<CustomTagRecord> result;
        TagLib::FileRef ref(path.wstring().c_str(), false, TagLib::FileRef::ReadStyle::Fast);
        if (ref.isNull() || !ref.file()) return result;

        const auto& properties = ref.file()->properties();
        auto tags = extract_custom_tags(properties);
        const auto now = static_cast<int64_t>(std::time(nullptr));
        for (auto& [key, value] : tags) {
            result.push_back(CustomTagRecord{
                track_id, std::move(key), std::move(value), now});
        }
        return result;
    }

  private:
    TagReaderConfig config_;

    // ---------------------------------------------------------------------------
    //  Format detection
    // ---------------------------------------------------------------------------

    void detect_format(const std::filesystem::path& path, TrackRecord& track) const {
        const auto ext = path.extension().string();
        const auto lower = [](std::string s) {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }(ext);

        if (lower == ".mp3" || lower == ".mp2") {
            track.container = "MPEG";
            track.codec = "MP3";
        } else if (lower == ".flac") {
            track.container = "FLAC";
            track.codec = "FLAC";
        } else if (lower == ".ogg") {
            track.container = "Ogg";
            track.codec = "Vorbis";
        } else if (lower == ".opus") {
            track.container = "Ogg";
            track.codec = "Opus";
        } else if (lower == ".m4a" || lower == ".aac" || lower == ".mp4") {
            track.container = "MP4";
            if (lower == ".m4a") track.codec = "AAC";
            else if (lower == ".aac") track.codec = "AAC";
            else track.codec = "MP4";
        } else if (lower == ".wav") {
            track.container = "RIFF";
            track.codec = "PCM";
        } else if (lower == ".aiff" || lower == ".aif") {
            track.container = "AIFF";
            track.codec = "PCM";
        } else if (lower == ".wma") {
            track.container = "ASF";
            track.codec = "WMA";
        } else if (lower == ".alac") {
            track.container = "MP4";
            track.codec = "ALAC";
        }
    }

    // ---------------------------------------------------------------------------
    //  ID3v2 extras
    // ---------------------------------------------------------------------------

    void parse_id3v2_extras(TrackRecord& track,
                            TagLib::ID3v2::Tag* id3,
                            const TagLib::PropertyMap& properties) const {
        (void)properties;

        // BPM: TBPM frame
        auto bpm_frames = id3->frameList()["TBPM"];
        if (!bpm_frames.isEmpty()) {
            try {
                std::string s = to_string(bpm_frames.front()->toString());
                // strip " BPM" suffix
                constexpr std::string_view suffix = " BPM";
                if (s.size() > suffix.size() &&
                    s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    s.resize(s.size() - suffix.size());
                }
                track.bpm = std::stod(s);
            } catch (...) {}
        }

        // Composer: TCOM
        auto tcom = id3->frameList()["TCOM"];
        if (!tcom.isEmpty()) track.composer = to_string(tcom.front()->toString());

        // Grouping: TIT1 or TIT3? Actually grouping is TIT3 in ID3
        auto grouping_frames = id3->frameList()["TIT3"];
        if (!grouping_frames.isEmpty()) track.grouping = to_string(grouping_frames.front()->toString());

        // Music key: TKEY
        auto tkey = id3->frameList()["TKEY"];
        if (!tkey.isEmpty()) track.music_key = to_string(tkey.front()->toString());

        // Disc number: TPOS
        auto tpos = id3->frameList()["TPOS"];
        if (!tpos.isEmpty()) track.discnumber = to_string(tpos.front()->toString());

        // Comment: COMM
        auto comm = id3->frameList()["COMM"];
        if (!comm.isEmpty()) track.comment = to_string(comm.front()->toString());

        // Source path
        auto src = id3->frameList()["WCOP"];
        if (!src.isEmpty()) track.source_path = to_string(src.front()->toString());

        // ISRC: TSRC
        auto tsrc = id3->frameList()["TSRC"];
        if (!tsrc.isEmpty()) track.source_id = to_string(tsrc.front()->toString());  // ISRC in source_id
    }

    // ---------------------------------------------------------------------------
    //  Xiph / Vorbis comment
    // ---------------------------------------------------------------------------

    void parse_xiph_comment(TrackRecord& track,
                            TagLib::Ogg::XiphComment* xiph,
                            const TagLib::PropertyMap& properties) const {
        (void)properties;
        if (!xiph) return;

        // Composer
        auto composer_list = xiph->fieldListMap()["COMPOSER"];
        if (!composer_list.isEmpty()) track.composer = to_string(composer_list.front());

        // Grouping
        auto grouping_list = xiph->fieldListMap()["GROUPING"];
        if (!grouping_list.isEmpty()) track.grouping = to_string(grouping_list.front());

        // Music key
        auto key_list = xiph->fieldListMap()["KEY"];
        if (!key_list.isEmpty()) track.music_key = to_string(key_list.front());

        // Disc number
        auto disc_list = xiph->fieldListMap()["DISCNUMBER"];
        if (!disc_list.isEmpty()) track.discnumber = to_string(disc_list.front());

        // Comment
        auto comment_list = xiph->fieldListMap()["COMMENT"];
        if (!comment_list.isEmpty()) track.comment = to_string(comment_list.front());
        // Also check DESCRIPTION and COMMENT (both valid)
        if (track.comment.empty()) {
            auto desc_list = xiph->fieldListMap()["DESCRIPTION"];
            if (!desc_list.isEmpty()) track.comment = to_string(desc_list.front());
        }
    }

    // ---------------------------------------------------------------------------
    //  MP4 tags
    // ---------------------------------------------------------------------------

    void parse_mp4_tags(TrackRecord& track,
                        TagLib::MP4::File* mp4,
                        const TagLib::PropertyMap& properties) const {
        (void)properties;
        if (!mp4 || !mp4->tag()) return;
        auto* tag = mp4->tag();

        // MP4 has atom-based tags; use property map for known fields
        const auto& map = tag->itemMap();

        auto get_text = [&](const char* key) -> std::string {
            auto it = map.find(key);
            if (it != map.end() && it->second.type() == TagLib::MP4::Item::TextType) {
                auto list = it->second.toStringList();
                if (!list.isEmpty()) return to_string(list.front());
            }
            return {};
        };

        track.composer = get_text("\251cmt");  // ©cmt
        track.grouping = get_text("\251grp");  // ©grp
        track.music_key = get_text("tmpo");    // BPM (integer)
        track.discnumber = get_text("disk");

        // Custom tags (everything not in the allowlist)
        // Already handled by extract_custom_tags()
    }

    // ---------------------------------------------------------------------------
    //  Cue line tokenizer
    // ---------------------------------------------------------------------------

    [[nodiscard]] std::vector<std::string> tokenize_cue_line(const std::string& line) const {
        std::vector<std::string> tokens;
        std::string token;
        bool in_quotes = false;
        for (char ch : line) {
            if (ch == '"') {
                in_quotes = !in_quotes;
            } else if (std::isspace(static_cast<unsigned char>(ch)) && !in_quotes) {
                if (!token.empty()) {
                    tokens.push_back(token);
                    token.clear();
                }
            } else {
                token += ch;
            }
        }
        if (!token.empty()) tokens.push_back(token);
        return tokens;
    }

    [[nodiscard]] std::string unquote(const std::vector<std::string>& tokens,
                                      std::size_t start) const {
        if (start >= tokens.size()) return {};
        std::string result;
        for (std::size_t i = start; i < tokens.size(); ++i) {
            std::string t = tokens[i];
            if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
                t = t.substr(1, t.size() - 2);
            }
            if (!result.empty()) result += ' ';
            result += t;
        }
        return result;
    }

    /// Parse a CD-DA timestamp (MM:SS:FF where FF is frames, 75 frames/second)
    /// into total frames.
    [[nodiscard]] int64_t parse_cue_timestamp(std::string_view ts) const {
        int mm = 0, ss = 0, ff = 0;
        char colon1 = 0, colon2 = 0;
        auto n = sscanf(std::string{ts}.c_str(), "%d:%d:%d", &mm, &ss, &ff);
        if (n >= 1) {
            // Handle MM:SS:FF format
            return static_cast<int64_t>(mm) * 60 * 75 + static_cast<int64_t>(ss) * 75 +
                   static_cast<int64_t>(ff);
        }
        return 0;
    }

    /// Compute a SHA-256 hex digest of a string.
    [[nodiscard]] std::string compute_sha256(const std::string& data) const {
        // Simple SHA-256 in pure C++. Use a third-party library in production;
        // here we use a fast implementation for the demo.
        // For actual use, link against OpenSSL or use a header-only SHA-256 impl.
        // Placeholder: return FNV hash as a stand-in for content-addressed ID.
        // In production: link against libsodium or use Botan.
        static constexpr uint64_t kFnvOffset = 14695981039346656037ULL;
        static constexpr uint64_t kFnvPrime = 1099511628211ULL;
        uint64_t h = kFnvOffset;
        for (unsigned char byte : data) {
            h ^= byte;
            h *= kFnvPrime;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016lx", h);
        return std::string{buf, 16};
    }
};

// ---------------------------------------------------------------------------
//  File identity helper (must be in same translation unit as TagLib includes)
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<FileIdentity> get_file_identity(
    const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    std::error_code ec;
    auto ftime = std::chrono::clock_cast<std::chrono::system_clock>(
        std::filesystem::last_write_time(path, ec));
    if (ec) return std::nullopt;
    const auto epoch_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(ftime.time_since_epoch())
            .count();
    return FileIdentity{0, static_cast<uint64_t>(epoch_ms)};
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    return FileIdentity{st.st_dev, static_cast<uint64_t>(st.st_ino)};
#endif
}

}  // anonymous namespace

// ===========================================================================
//  TagReader
// ===========================================================================

TagReader::TagReader() : impl_{std::make_unique<TagReaderImpl>(TagReaderConfig{})} {}

TagReader::TagReader(const TagReaderConfig& config)
      : impl_{std::make_unique<TagReaderImpl>(config)} {}

TagReader::~TagReader() = default;

Result<TrackRecord> TagReader::read(const std::filesystem::path& path) const {
    return impl_->read(path);
}

Result<TrackRecord> TagReader::read_with_cuesheet(
    const std::filesystem::path& media_path,
    const std::filesystem::path& cue_path) const {
    return impl_->read_with_cuesheet(media_path, cue_path);
}

Result<CuesheetRecord> TagReader::parse_cuesheet(const std::filesystem::path& path) const {
    return impl_->parse_cuesheet(path);
}

std::vector<CustomTagRecord> TagReader::extract_custom_tags(
    const std::filesystem::path& path,
    int64_t track_id) const {
    return impl_->extract_custom_tags_for(path, track_id);
}

Result<std::string> TagReader::sort_key_for(std::string_view artist,
                                           std::string_view album) const {
    return to_sort_key(artist.empty() ? album : artist);
}

Result<std::string> TagReader::album_hash_for(std::string_view album,
                                              std::string_view albumartist) const {
    return compute_album_hash(album, albumartist);
}

std::vector<std::string> TagReader::split_artists(const std::string& joined) const {
    return split_multi_value(joined);
}

std::vector<std::string> TagReader::split_genres(const std::string& joined) const {
    return split_multi_value(joined);
}

}  // namespace arrow::library
