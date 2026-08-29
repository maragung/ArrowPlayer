// SPDX-License-Identifier: MPL-2.0
//
// manifest.cpp — see manifest.hpp for design notes.

#include "skin/manifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <miniz.h>   // header-only zip: desktop/third_party/miniz.h

#include <nlohmann/json.hpp>

namespace arrow::skin {

namespace {

// ---------------------------------------------------------------------------
//  SHA-256 helpers
// ---------------------------------------------------------------------------

// A minimal SHA-256 implementation using the public domain code from
// the RFC 4634 "SHA-256 Example" (equivalent to the FIPS 180-4 reference).
// We use this instead of a library to keep the skin engine dependency-free
// and to avoid pulling in crypto libraries that would complicate licensing.
struct Sha256 {
    std::array<std::uint32_t, 8> h{};
    std::array<std::uint8_t, 64> block{};
    std::size_t block_len{0};
    std::uint64_t total_bits{0};

    Sha256();

    void update(std::string_view data);
    void finalise(std::array<std::uint8_t, 32>& out);

private:
    static constexpr std::array<std::uint32_t, 64> K = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    void process_block();
    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }
};

Sha256::Sha256() {
    h[0] = 0x6a09e667u; h[1] = 0xbb67ae85u;
    h[2] = 0x3c6ef372u; h[3] = 0xa54ff53au;
    h[4] = 0x510e527fu; h[5] = 0x9b05688cu;
    h[6] = 0x1f83d9abu; h[7] = 0x5be0cd19u;
}

void Sha256::update(std::string_view data) {
    for (char c : data) {
        block[block_len++] = static_cast<std::uint8_t>(c);
        if (block_len == 64) {
            process_block();
            block_len = 0;
        }
        total_bits += 8;
    }
}

void Sha256::finalise(std::array<std::uint8_t, 32>& out) {
    // Pad: 1 bit, zeros, then 64-bit big-endian length
    block[block_len++] = 0x80;
    if (block_len > 56) {
        // Need two blocks
        while (block_len < 64) block[block_len++] = 0;
        process_block();
        block_len = 0;
    }
    while (block_len < 56) block[block_len++] = 0;
    // Write total_bits in big-endian at the end
    for (int i = 0; i < 8; ++i)
        block[63 - i] = static_cast<std::uint8_t>((total_bits >> (i * 8)) & 0xff);
    process_block();
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xff);
        out[i * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xff);
        out[i * 4 + 2] = static_cast<std::uint8_t>((h[i] >>  8) & 0xff);
        out[i * 4 + 3] = static_cast<std::uint8_t>((h[i] >>  0) & 0xff);
    }
}

void Sha256::process_block() {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) <<  8) |
               (static_cast<std::uint32_t>(block[i * 4 + 3]) <<  0);
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto hh = h;
    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = rotr(hh[4], 6) ^ rotr(hh[4], 11) ^ rotr(hh[4], 25);
        std::uint32_t ch = (hh[4] & hh[5]) ^ (~hh[4] & hh[6]);
        std::uint32_t temp1 = hh[7] + S1 + ch + K[i] + w[i];
        std::uint32_t S0 = rotr(hh[0], 2) ^ rotr(hh[0], 13) ^ rotr(hh[0], 22);
        std::uint32_t maj = (hh[0] & hh[1]) ^ (hh[0] & hh[2]) ^ (hh[1] & hh[2]);
        std::uint32_t temp2 = S0 + maj;
        hh[7] = hh[6];
        hh[6] = hh[5];
        hh[5] = hh[4];
        hh[4] = hh[3] + temp1;
        hh[3] = hh[2];
        hh[2] = hh[1];
        hh[1] = hh[0];
        hh[0] = temp1 + temp2;
    }
    for (int i = 0; i < 8; ++i) h[i] += hh[i];
}

// ---------------------------------------------------------------------------
//  ChecksumEntry
// ---------------------------------------------------------------------------

std::optional<ChecksumEntry> ChecksumEntry::parse(std::string_view hex) noexcept {
    if (hex.size() != 64) return std::nullopt;
    ChecksumEntry e;
    e.hex = std::string{hex};
    for (std::size_t i = 0; i < 32; ++i) {
        auto hi = hex.substr(i * 2, 2);
        std::uint8_t byte = 0;
        if (std::from_chars(hi.data(), hi.data() + 2, byte, 16).ec != std::errc{})
            return std::nullopt;
        e.digest[i] = byte;
    }
    return e;
}

bool ChecksumEntry::verify(std::string_view data) const noexcept {
    std::array<std::uint8_t, 32> computed{};
    Sha256 sha;
    sha.update(data);
    sha.finalise(computed);
    return computed == digest;
}

// ---------------------------------------------------------------------------
//  Surface parsing
// ---------------------------------------------------------------------------

std::optional<Surface> parse_surface(std::string_view s) noexcept {
    if (s == "main-window") return Surface::main_window;
    if (s == "now-playing") return Surface::now_playing;
    if (s == "mini-player") return Surface::mini_player;
    if (s == "library")     return Surface::library;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
//  Version parsing
// ---------------------------------------------------------------------------

std::optional<std::array<std::int32_t, 3>> parse_version(std::string_view s) noexcept {
    std::array<std::int32_t, 3> v{0, 0, 0};
    int part = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            auto chunk = s.substr(start, i - start);
            if (std::from_chars(chunk.data(), chunk.data() + chunk.size(), v[part]).ec != std::errc{})
                return std::nullopt;
            ++part;
            start = i + 1;
            if (part == 3) break;
        }
    }
    if (part != 2) return std::nullopt;  // need exactly two dots
    return v;
}

bool check_min_app_version(std::string_view skin_min, std::string_view app_version) noexcept {
    auto skin_v = parse_version(skin_min);
    auto app_v  = parse_version(app_version);
    if (!skin_v || !app_v) return false;
    for (int i = 0; i < 3; ++i) {
        if (app_v->at(i) < skin_v->at(i)) return false;
        if (app_v->at(i) > skin_v->at(i)) return true;
    }
    return true;  // equal
}

bool check_schema_compatibility(std::int32_t skin_sv, std::int32_t engine_sv) noexcept {
    // For v1: only schema version 1 is supported.  A skin declaring a newer
    // schema is rejected; a skin declaring an older schema is migrated.
    if (skin_sv > engine_sv) return false;  // newer than we know
    return true;
}

// ---------------------------------------------------------------------------
//  Path safety helpers (REQ-THM-018)
//
//  Every path in the archive must be:
//    - Relative (not starting with / or a drive letter)
//    - Free of ".." segments
//    - Free of NUL and control characters
//    - Confined to a permitted top-level directory
//    - ≤ 200 bytes
// ---------------------------------------------------------------------------

bool is_path_safe(std::string_view path) noexcept {
    if (path.empty()) return false;
    if (path.size() > 200) return false;

    // No absolute prefixes, no drive letters, no NUL
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.size() >= 3 && std::isalpha(path[0]) && path[1] == ':') return false;
    for (char c : path) {
        if (c == '\0') return false;
        if (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\r' && c != '\t')
            return false;
    }

    // No ".." segments (zip-slip prevention)
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (path[i] == '.' && path[i + 1] == '.') {
            // Check it's not the start of a ".." directory name
            if (i == 0 || path[i - 1] == '/') {
                // ".." at the beginning or after a slash
                // But we need to check it's not part of a longer name like ".dotfile"
                std::size_t j = i + 2;
                if (j >= path.size() || path[j] == '/')
                    return false;  // it's a real ".." segment
            }
        }
    }

    // Must be in a permitted top-level directory
    static constexpr std::string_view kAllowedPrefixes[] = {
        "theme.json", "LICENSE", "preview.png",
        "layout/", "icons/", "images/", "fonts/", "i18n/",
    };
    bool allowed = false;
    for (auto prefix : kAllowedPrefixes) {
        if (path.starts_with(prefix)) {
            allowed = true;
            break;
        }
    }
    // Also allow the preview.png and LICENSE without directory
    if (!allowed && (path == "preview.png" || path == "LICENSE")) {
        allowed = true;
    }
    return allowed;
}

// ---------------------------------------------------------------------------
//  miniz ZIP helpers
// ---------------------------------------------------------------------------

// Lightweight wrapper around miniz that reads manifest.json from a ZIP in memory.
Result<nlohmann::json, std::string> read_manifest_from_zip(std::string_view zip_data) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zip_data.data(), zip_data.size(), 0)) {
        return Result<nlohmann::json, std::string>::failure(
            "failed to open ZIP archive: " + std::string{mz_zip_get_error_string(mz_zip_get_last_error(&zip))}
        );
    }

    auto guard = mz_zip_end;  // RAII-like cleanup via defer

    // Count entries and check limits
    std::int64_t num_files = mz_zip_reader_get_num_files(&zip);
    if (num_files > 2000) {
        return Result<nlohmann::json, std::string>::failure(
            "skin package contains " + std::to_string(num_files) +
            " entries; the maximum is 2000 (REQ-THM-017)"
        );
    }

    // Find manifest.json
    int manifest_index = -1;
    std::vector<std::pair<std::string, std::int64_t>> all_entries;  // path, uncompressed size

    for (std::int64_t i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

        std::string entry_path{stat.m_filename};
        // Normalise: replace backslashes with forward slashes
        for (char& c : entry_path) if (c == '\\') c = '/';

        // Check path safety
        if (!is_path_safe(entry_path)) {
            return Result<nlohmann::json, std::string>::failure(
                "unsafe path in skin package: '" + entry_path +
                "' — paths must be relative, confined to permitted directories, "
                "and free of '..' segments (REQ-THM-018)"
            );
        }

        // Record for size checking
        all_entries.emplace_back(entry_path, stat.m_uncompressed_size);

        if (entry_path == "manifest.json") {
            manifest_index = static_cast<int>(i);
        }
    }

    // Check per-file size limits
    for (auto& [path, size] : all_entries) {
        static constexpr std::int64_t MAX_FILE = 8 * 1024 * 1024;  // 8 MiB
        if (size > MAX_FILE) {
            return Result<nlohmann::json, std::string>::failure(
                "file '" + path + "' is " + std::to_string(size) +
                " bytes; single-file limit is 8 MiB (REQ-THM-017)"
            );
        }
    }

    if (manifest_index < 0) {
        return Result<nlohmann::json, std::string>::failure(
            "skin package has no manifest.json (REQ-THM-015 requires it)"
        );
    }

    // Read and decompress manifest.json
    std::size_t manifest_size = 0;
    void* manifest_data = mz_zip_reader_extract_to_heap(&zip, manifest_index, &manifest_size, 0);
    if (!manifest_data) {
        return Result<nlohmann::json, std::string>::failure(
            "failed to read manifest.json from skin package"
        );
    }

    std::string manifest_text(
        static_cast<const char*>(manifest_data),
        manifest_size
    );
    mz_free(manifest_data);

    try {
        auto doc = nlohmann::json::parse(manifest_text);
        (void)guard;  // silence unused warning
        // Note: guard's destructor is not actually a defer; we use RAII below
        mz_zip_reader_end(&zip);
        return Result<nlohmann::json, std::string>::success(std::move(doc));
    } catch (const std::exception& e) {
        mz_zip_reader_end(&zip);
        return Result<nlohmann::json, std::string>::failure(
            std::string{"manifest.json is not valid JSON: "} + e.what()
        );
    }
}

// ---------------------------------------------------------------------------
//  Build SkinManifest from parsed JSON
// ---------------------------------------------------------------------------

Result<SkinManifest, std::string> build_manifest(
    const nlohmann::json& doc,
    const std::filesystem::path& source_path) noexcept {

    SkinManifest m;
    m.source_path = source_path;

    auto get_str = [&](const char* key) -> std::optional<std::string> {
        auto it = doc.find(key);
        if (it == doc.end()) return std::nullopt;
        if (!it->is_string()) return std::nullopt;
        return it->get<std::string>();
    };

    auto get_int = [&](const char* key, std::int32_t default_val) -> std::int32_t {
        auto it = doc.find(key);
        if (it == doc.end()) return default_val;
        if (it->is_number_integer()) return it->get<std::int32_t>();
        return default_val;
    };

    auto get_required_str = [&](const char* key) -> Result<std::string, std::string> {
        auto v = get_str(key);
        if (!v) return Result<std::string, std::string>::failure(
            std::string{"manifest.json: missing required field '"} + key + "'"
        );
        return Result<std::string, std::string>::success(std::move(*v));
    };

    // schemaVersion
    m.schema_version = get_int("schemaVersion", 1);

    // id
    {
        auto r = get_required_str("id");
        if (!r) return Result<SkinManifest, std::string>::failure(r.error);
        m.id = std::move(r.value);
    }

    // name
    {
        auto r = get_required_str("name");
        if (!r) return Result<SkinManifest, std::string>::failure(r.error);
        m.name = std::move(r.value);
    }

    m.author   = get_str("author");
    {
        auto r = get_required_str("version");
        if (!r) return Result<SkinManifest, std::string>::failure(r.error);
        m.version = std::move(r.value);
    }
    m.license     = get_str("license");
    m.homepage    = get_str("homepage");
    m.description = get_str("description");

    // minAppVersion (required)
    {
        auto r = get_required_str("minAppVersion");
        if (!r) return Result<SkinManifest, std::string>::failure(r.error);
        m.min_app_version = std::move(r.value);
    }

    // schemaVersion string (for display)
    if (auto sv = get_str("schemaVersion"))
        m.schema_version_str = *sv;

    // capabilities (required)
    auto cap_it = doc.find("capabilities");
    if (cap_it == doc.end() || !cap_it->is_array()) {
        return Result<SkinManifest, std::string>::failure(
            "manifest.json: missing required field 'capabilities'"
        );
    }
    for (const auto& c : *cap_it) {
        if (!c.is_string()) continue;
        std::string_view sv = c.get<std::string>();
        if (sv == "theme")  m.capabilities.push_back(Capability::theme);
        else if (sv == "layout")  m.capabilities.push_back(Capability::layout);
        else if (sv == "icons")  m.capabilities.push_back(Capability::icons);
        else if (sv == "images") m.capabilities.push_back(Capability::images);
        else if (sv == "fonts")  m.capabilities.push_back(Capability::fonts);
        // Unknown capability: ignore it (forward compatibility)
    }

    // targetSurfaces
    auto surf_it = doc.find("targetSurfaces");
    if (surf_it != doc.end() && surf_it->is_array()) {
        for (const auto& s : *surf_it) {
            if (s.is_string()) {
                if (auto surf = parse_surface(s.get<std::string>()))
                    m.target_surfaces.push_back(*surf);
            }
        }
    }

    // preview (fixed, but read for completeness)
    if (auto pv = get_str("preview")) m.preview = *pv;

    // i18n
    auto i18n_it = doc.find("i18n");
    if (i18n_it != doc.end() && i18n_it->is_array()) {
        for (const auto& t : *i18n_it) {
            if (t.is_string())
                m.i18n.push_back(t.get<std::string>());
        }
    }

    // checksums
    auto ck_it = doc.find("checksums");
    if (ck_it != doc.end() && ck_it->is_object()) {
        for (auto& [path, digest_hex] : ck_it->items()) {
            if (!digest_hex.is_string()) continue;
            if (auto entry = ChecksumEntry::parse(digest_hex.get<std::string>())) {
                m.checksums.emplace(path, std::move(*entry));
            }
        }
    }

    return Result<SkinManifest, std::string>::success(std::move(m));
}

}  // namespace

// ---------------------------------------------------------------------------
//  Public entry points
// ---------------------------------------------------------------------------

Result<SkinManifest, std::string> ManifestReader::read(
    const std::filesystem::path& archive_path,
    const theme::SchemaValidator& validator) noexcept {

    std::ifstream in{archive_path, std::ios::binary};
    if (!in) {
        return Result<SkinManifest, std::string>::failure(
            "cannot open skin package: " + archive_path.string()
        );
    }
    std::string zip_data{std::istreambuf_iterator<char>{in}, {}};
    return parse(zip_data, validator);
}

Result<SkinManifest, std::string> ManifestReader::parse(
    std::string_view zip_data,
    const theme::SchemaValidator& validator) noexcept {

    // Step 1: extract and basic-validate the ZIP structure
    auto json_result = read_manifest_from_zip(zip_data);
    if (!json_result) {
        return Result<SkinManifest, std::string>::failure(std::move(json_result.error));
    }

    // Step 2: schema-validate manifest.json
    auto schema_result = validator.validate(theme::SchemaId::SkinManifest, json_result.value);
    if (!schema_result.ok()) {
        std::string err = "manifest.json failed schema validation:\n";
        for (auto& e : schema_result.errors) {
            err += "  " + e.instance_pointer + ": " + e.message + "\n";
        }
        // Strip trailing newline
        if (!err.empty() && err.back() == '\n') err.pop_back();
        return Result<SkinManifest, std::string>::failure(std::move(err));
    }

    // Step 3: build typed struct
    auto build_result = build_manifest(json_result.value, {});
    if (!build_result) {
        return Result<SkinManifest, std::string>::failure(std::move(build_result.error));
    }

    return Result<SkinManifest, std::string>::success(std::move(build_result.value));
}

}  // namespace arrow::skin
