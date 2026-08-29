// SPDX-License-Identifier: MPL-2.0
//
// manifest.hpp — Skin manifest (.eclipseskin) reader and validator.
//
// Spec: eclipse-player.md §11.3 (REQ-THM-015 … REQ-THM-019).
//
// A skin package is a ZIP archive with the extension `.eclipseskin`.
// This module reads and validates the `manifest.json` at its root.
//
// Hard limits enforced here (REQ-THM-017):
//   - Total uncompressed size (32 MiB) — enforced by ZipSafety on extract
//   - Entry count (2000)              — enforced by ZipSafety on extract
//   - Single file uncompressed (8 MiB) — enforced by ZipSafety on extract
//   - Path depth (4)                  — enforced here on parse
//   - Path length (200 bytes)          — enforced here on parse
//
// The other limits — compression ratio 100:1, image dimensions 8192×8192,
// font count 8, layout documents 16 — are enforced by the installer.
// Security controls (zip-slip, no symlinks, no hard links, no devices)
// are enforced by the zip_safety module.
//
// Thread-safety: SkinManifest is immutable after construction.  Multiple
// threads may read it concurrently without synchronisation.

#pragma once

#include <array>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "theme/schema.hpp"

namespace arrow::skin {

// ---------------------------------------------------------------------------
//  Surface types that a skin can override
// ---------------------------------------------------------------------------

enum class Surface : std::uint8_t {
    main_window,
    now_playing,
    mini_player,
    library,
};

constexpr std::string_view to_string(Surface s) noexcept {
    switch (s) {
        case Surface::main_window:  return "main-window";
        case Surface::now_playing:   return "now-playing";
        case Surface::mini_player:   return "mini-player";
        case Surface::library:       return "library";
    }
    return "<unknown>";
}

std::optional<Surface> parse_surface(std::string_view s) noexcept;

// ---------------------------------------------------------------------------
//  Capability declaration
//
//  §11.3: a manifest declares which tiers it uses.  A capability not listed
//  here is not honoured even if the corresponding directory is present.
// ---------------------------------------------------------------------------

enum class Capability : std::uint8_t {
    theme,   ///< Contains a theme.json
    layout,  ///< Contains layout documents
    icons,   ///< Contains SVG icons
    images,  ///< Contains raster images
    fonts,   ///< Contains TTF/OTF/WOFF2 fonts
};

constexpr std::string_view to_string(Capability c) noexcept {
    switch (c) {
        case Capability::theme:  return "theme";
        case Capability::layout:  return "layout";
        case Capability::icons:  return "icons";
        case Capability::images: return "images";
        case Capability::fonts:  return "fonts";
    }
    return "<unknown>";
}

// ---------------------------------------------------------------------------
//  Asset checksum entry
//
//  manifest.json.checksums maps a package-relative path (e.g. "theme.json")
//  to its SHA-256 (lower-case hex).  §11.3: the installer verifies every
//  entry and refuses a package containing a file absent from this map.
// ---------------------------------------------------------------------------

struct ChecksumEntry {
    std::array<std::uint8_t, 32> digest;  // binary SHA-256
    std::string                   hex;       // lower-case hex, for display

    /// Parse a 64-character lower-case hex string into a ChecksumEntry.
    /// Returns nullopt if the string is not a valid SHA-256 hex digest.
    static std::optional<ChecksumEntry> parse(std::string_view hex) noexcept;

    /// Return true if this digest matches `data`.
    bool verify(std::string_view data) const noexcept;
};

// ---------------------------------------------------------------------------
//  SkinManifest
//
//  The validated contents of a skin's manifest.json.  All strings are
//  stored as-is; no further validation is performed here.
// ---------------------------------------------------------------------------

struct SkinManifest {
    // Identity
    std::int32_t  schema_version{1};
    std::string    id;            // [a-z0-9]([a-z0-9-]{1,62}[a-z0-9])?
    std::string    name;          // 1..64 chars
    std::optional<std::string> author;
    std::string    version;       // X.Y.Z
    std::optional<std::string> license;       // SPDX identifier
    std::optional<std::string> homepage;
    std::optional<std::string> description;

    // Compatibility
    std::string    min_app_version;  // X.Y.Z — skin requires this app version
    std::string    schema_version_str;  // original string "1"

    // Capabilities (required)
    std::vector<Capability> capabilities;

    // Overridden surfaces
    std::vector<Surface>  target_surfaces;

    // Package metadata
    std::string    preview;  // "preview.png" — fixed by the schema

    // Supported i18n language tags
    std::vector<std::string> i18n;

    // Checksums: path → digest.  Every file in the package except
    // manifest.json itself must be listed here (REQ-THM-016).
    std::unordered_map<std::string, ChecksumEntry> checksums;

    // The path to the .eclipseskin file on disk (if loaded from a file).
    std::filesystem::path source_path;

    /// Return true if this manifest declares a given capability.
    bool has_capability(Capability c) const noexcept {
        return std::find(capabilities.begin(), capabilities.end(), c) != capabilities.end();
    }

    /// Return true if this manifest overrides the given surface.
    bool overrides_surface(Surface s) const noexcept {
        return std::find(target_surfaces.begin(), target_surfaces.end(), s) != target_surfaces.end();
    }
};

// ---------------------------------------------------------------------------
//  ManifestReader
//
//  Reads and validates a .eclipseskin archive.  Steps:
//    1. Open the ZIP and enumerate entries (limits checked).
//    2. Read and validate manifest.json against skin-manifest.schema.json.
//    3. Return a validated SkinManifest.
//
//  The ZIP is read sequentially; large files are not fully decompressed
//  unless needed.  Checksum verification of individual assets is deferred
//  to the installer (which needs the asset bytes anyway).
// ---------------------------------------------------------------------------

class ManifestReader {
public:
    /// Read and validate the manifest from a .eclipseskin archive.
    /// Returns a populated SkinManifest on success, or an error message.
    static Result<SkinManifest, std::string> read(
        const std::filesystem::path& archive_path,
        const theme::SchemaValidator& validator) noexcept;

    /// Read and validate the manifest from an in-memory ZIP archive.
    /// `zip_data` must be the complete bytes of the ZIP file.
    static Result<SkinManifest, std::string> parse(
        std::string_view zip_data,
        const theme::SchemaValidator& validator) noexcept;

private:
    static Result<SkinManifest, std::string> build(
        const nlohmann::json& doc,
        const std::filesystem::path& source_path,
        const theme::SchemaValidator& validator) noexcept;
};

// ---------------------------------------------------------------------------
//  Version compatibility helpers
// ---------------------------------------------------------------------------

/// Parse a "X.Y.Z" version string.  Returns nullopt if malformed.
std::optional<std::array<std::int32_t, 3>> parse_version(std::string_view s) noexcept;

/// Check whether `skin_min` (the skin's declared minimum app version) is
/// satisfied by `app_version` (the running app's version).
/// Returns true if the skin is compatible.
bool check_min_app_version(std::string_view skin_min,
                          std::string_view app_version) noexcept;

/// Check whether the skin's schema version is compatible with the engine's.
/// The schema version in the manifest is always "1" for v1.0 skins; if the
/// engine ever bumps the schema it will need a migration path.
bool check_schema_compatibility(std::int32_t skin_schema_version,
                              std::int32_t engine_schema_version) noexcept;

// ---------------------------------------------------------------------------
//  Result type (lightweight, no std::expected availability)
// ---------------------------------------------------------------------------

template <typename T, typename E>
struct Result {
    T      value;
    E      error;
    bool   ok;

    static Result success(T v) { return {std::move(v), {}, true}; }
    static Result failure(E e) { return {{}, std::move(e), false}; }

    explicit operator bool() const noexcept { return ok; }
    T&       operator*()       noexcept { return value; }
    const T& operator*() const noexcept { return value; }
};

}  // namespace arrow::skin
