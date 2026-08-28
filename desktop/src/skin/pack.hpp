// SPDX-License-Identifier: MPL-2.0
//
// pack.hpp — .eclipseskin package installer.
//
// Spec: eclipse-player.md §11.3 (REQ-THM-015 … REQ-THM-019), §11.5
// (REQ-THM-040, steps 1, 2, 3, 9).
//
// A `.eclipseskin` is a ZIP archive laid out as documented in REQ-THM-015.
// The installer is a state machine:
//
//   1. Open the archive and walk the central directory.
//      Reject if any REQ-THM-017 limit is exceeded
//      (size, count, ratio, per-file, depth, path length, fonts, layouts).
//   2. For every entry, run the path-safety check (zip_safety).
//   3. Read manifest.json, validate against the skin-manifest schema,
//      and verify the SHA-256 of every other entry against `checksums`.
//   4. Verify that every file in the archive is listed in `checksums`
//      (and that nothing else is). This is the inverse check and is
//      what makes the manifest a true integrity claim.
//   5. If everything passes, the install is atomic: extract to a temp
//      directory, then rename into place. A failure at any step
//      removes the temp directory and leaves the previous install
//      untouched.
//
// The pack header here is the *public* surface; the rest of the app
// (the in-app browser, the CLI, the test suite) goes through `install`.
// We deliberately do not expose individual stages: there is no use
// case for half-installing a package, and exposing a "skip step 3"
// knob would let a caller bypass the only thing standing between an
// attacker and a hot path on the render thread.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "skin/zip_safety.hpp"
#include "theme/loader.hpp"
#include "theme/schema.hpp"

namespace arrow::skin {

// ---------------------------------------------------------------------------
//  REQ-THM-017 hard limits, surfaced as constants so the test suite and
//  any other consumer of the installer share the same numbers.
// ---------------------------------------------------------------------------

inline constexpr std::uint64_t kMaxUncompressedTotal     = 32ULL * 1024 * 1024;  // 32 MiB
inline constexpr std::uint64_t kMaxSingleFileUncompressed = 8ULL  * 1024 * 1024;  //  8 MiB
inline constexpr std::size_t   kMaxEntries               = 2000;
inline constexpr std::size_t   kMaxFonts                 = 8;
inline constexpr std::size_t   kMaxLayoutDocs            = 16;
inline constexpr std::uint32_t kMaxCompressionRatio     = 100;  // 100:1

// ---------------------------------------------------------------------------
//  Per-entry archive metadata. Built by reading the central directory.
// ---------------------------------------------------------------------------

struct EntryMeta {
    std::string     name;
    std::uint64_t   uncompressed_size{0};
    std::uint64_t   compressed_size{0};
    std::uint32_t   crc32{0};
    std::uint32_t   external_attr{0};
    bool            is_symlink{false};
    bool            is_hardlink{false};
    bool            is_device{false};
    bool            is_directory{false};
};

// ---------------------------------------------------------------------------
//  Install verdict. REQ-THM-040 says the pipeline stops at the first
//  failure; the result struct names the failing step so the UI can
//  show the right kind of error.
// ---------------------------------------------------------------------------

enum class InstallError {
    None,
    OpenFailed,                // could not open the file
    NotAZip,                   // signature missing
    CentralDirFailed,          // central directory unreadable
    LimitsExceeded,            // REQ-THM-017 hard limit
    PathSafety,                // REQ-THM-018
    ManifestMissing,           // no manifest.json
    ManifestSchema,            // manifest failed schema validation
    ChecksumMissing,           // manifest has no checksums block
    ChecksumMismatch,          // an entry's digest does not match
    UnlistedFile,              // an entry exists but is not in checksums
    UnknownFileInChecksums,    // a checksum is declared for a path that does not exist
    ManifestSelfChecksum,      // manifest.json listed in its own checksums
    ThemeSchema,               // theme.json failed schema validation
    AtomicRenameFailed,        // temp->final rename failed
    ManifestMinAppVersion,     // package is for a newer app
    FontLicenceMissing,        // fonts/ present without fonts/LICENSE-fonts
};

struct InstallResult {
    InstallError   error{InstallError::None};
    std::string    why;                // human-readable
    std::string    offending_pointer;  // JSON Pointer or entry name, when relevant
    std::filesystem::path installed_to; // final on-disk install location on success

    bool ok() const noexcept { return error == InstallError::None; }
};

// ---------------------------------------------------------------------------
//  The package descriptor: what manifest.json, after validation, looks
//  like to the rest of the app. We do not expose the raw nlohmann::json
//  because callers should not be poking at the manifest after install.
// ---------------------------------------------------------------------------

struct PackageInfo {
    std::string                  id;
    std::string                  name;
    std::string                  version;
    std::string                  license;
    std::string                  min_app_version;
    std::vector<std::string>     capabilities;
    std::vector<std::string>     target_surfaces;
    std::vector<std::string>     i18n;
    std::optional<std::string>   homepage;
    std::optional<std::string>   description;
};

// ---------------------------------------------------------------------------
//  Installer
// ---------------------------------------------------------------------------

class SkinInstaller {
public:
    // The validator is used for both the manifest schema and the theme
    // schema. It is borrowed; the caller owns it and keeps it alive for
    // the duration of the install.
    explicit SkinInstaller(const theme::SchemaValidator& validator);

    // Open the archive at `path` and run steps 1–3 of the pipeline.
    // On success, `entries` is filled with the central-directory
    // entries and `manifest` carries the parsed manifest.json. The
    // actual extraction (steps 4–5) is performed by `install` below.
    InstallResult inspect(const std::filesystem::path& path,
                          std::vector<EntryMeta>& entries,
                          PackageInfo& manifest);

    // Read the embedded theme.json out of a previously-inspected
    // package. Returns nullptr if the package does not carry a theme.
    // (Themes alone are a valid capability; themes+layouts is the
    // common case.)
    std::shared_ptr<const theme::Theme> extract_theme(
        const std::filesystem::path& path);

    // Full install. Calls inspect() internally; on success extracts
    // every entry to a temp directory, then renames atomically into
    // `destination_root/<id>-<version>/`. A failure at any step
    // removes the temp directory and leaves the previous install
    // untouched.
    InstallResult install(const std::filesystem::path& path,
                          const std::filesystem::path& destination_root);

    // The current app version, as a "X.Y.Z" string. Compared against
    // the package's minAppVersion. Set by the caller; the installer
    // does not assume a particular build version.
    void set_app_version(const std::string& v) { app_version_ = v; }

private:
    const theme::SchemaValidator& validator_;
    std::string                   app_version_;
};

}  // namespace arrow::skin
