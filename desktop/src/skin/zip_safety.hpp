// SPDX-License-Identifier: MPL-2.0
//
// zip_safety.hpp — REQ-THM-018 zip-slip prevention.
//
// Spec: eclipse-player.md §11.3 (REQ-THM-018) and §11.5 (REQ-THM-040
// step 2). Every entry in a .eclipseskin archive MUST be:
//
//   * relative,
//   * normalised,
//   * free of `..` segments,
//   * free of absolute prefixes and drive letters,
//   * free of NUL and control characters,
//   * not a symlink, not a hard link, not a device node,
//   * confined to one of the permitted top-level directories,
//   * and the resolved destination path MUST lie inside the install
//     root — a checking rule, not a denylist, because the only
//     denylist that ever worked was the one that stopped trying.
//
// We use a small zlib-based ZIP reader rather than depending on the
// system zip(1); the spec calls for a fuzz target and libzip's API
// surface is not fuzz-friendly. The point of the safety checks is
// that they reject every crafted payload in shared-spec/conformance/
// theme-validation-cases/malicious/zip-entry-paths.json — that table
// is what makes the requirement testable.
//
// This file is the only place that holds the policy; pack.hpp asks
// zip_safety whether each entry is acceptable and never inspects the
// path itself.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace arrow::skin {

// ---------------------------------------------------------------------------
//  Per-entry result of one safety check. The result is "ok" or one of
//  the specific rejection reasons. The string `why` is the human-readable
//  explanation; machine-readable verdicts go through the enum.
// ---------------------------------------------------------------------------

enum class Verdict : std::uint8_t {
    Ok,
    EmptyPath,                  // the archive entry has no name
    AbsolutePath,               // starts with '/' (Unix) or a drive letter / UNC (Windows)
    BackslashSeparator,         // uses '\\' as a separator
    DotDotSegment,              // contains '..' as a path component
    NullByte,                   // contains a NUL
    ControlChar,                // contains a non-NUL control byte (< 0x20 except tab)
    UnlistedTopLevel,           // first segment is not in kPermittedTopLevel
    DepthExceeded,              // more than 4 path components
    PathTooLong,                // > 200 bytes (REQ-THM-017)
    SymlinkEntry,               // external_attr says it is a symlink
    HardLinkEntry,              // external_attr says it is a hard link
    DeviceNode,                 // external_attr says it is a block/char device
    EscapesDestination,         // resolved path would leave the install root
};

struct SafetyResult {
    Verdict       verdict{Verdict::Ok};
    std::string   why;        // human-readable, empty on Ok

    bool ok() const noexcept { return verdict == Verdict::Ok; }
    static SafetyResult accept() { return {}; }
    static SafetyResult reject(Verdict v, std::string reason) {
        return SafetyResult{v, std::move(reason)};
    }
};

// ---------------------------------------------------------------------------
//  Permitted top-level directories inside a .eclipseskin package.
//
//  The packagePath pattern in skin-manifest.schema.json is the source
//  of truth; this table MUST stay in sync with it.
// ---------------------------------------------------------------------------

inline constexpr std::array<std::string_view, 8> kPermittedTopLevel = {
    "theme.json",
    "LICENSE",
    "preview.png",
    "layout/",
    "icons/",
    "images/",
    "fonts/",
    "i18n/",
};

// Maximum path length and maximum path depth, both REQ-THM-017.
inline constexpr std::size_t kMaxPathLength  = 200;
inline constexpr std::size_t kMaxPathDepth   = 4;

// ---------------------------------------------------------------------------
//  The path-safety check. `entry_name` is the raw filename field from
//  the central directory; `kind` and `mode` come from the entry's
//  external_attr; `root` is the directory the entry will be extracted
//  into. The function never touches the filesystem for the path check
//  itself; the only filesystem call is the canonicalisation of the
//  resolved destination, which is what catches the trickiest escape
//  payloads (a symlink left inside the destination by a previous
//  extract).
// ---------------------------------------------------------------------------

struct EntryAttributes {
    bool is_symlink{false};
    bool is_hardlink{false};
    bool is_device{false};
};

SafetyResult check_entry_path(const std::string& entry_name,
                              const EntryAttributes& attr,
                              const std::filesystem::path& destination_root);

// ---------------------------------------------------------------------------
//  Convenience wrapper: take a vector of raw entry names and their
//  attributes, run the safety check on each, and return a vector of
//  per-entry results. Returns the index of the first failing entry, or
//  std::string::npos if every entry passes. The first-fail index is
//  what REQ-THM-040 step 2 reports back.
// ---------------------------------------------------------------------------

struct EntryInfo {
    std::string     name;
    EntryAttributes attr{};
};

struct BulkResult {
    bool                          all_ok{true};
    std::size_t                   first_fail{static_cast<std::size_t>(-1)};
    std::vector<SafetyResult>     per_entry;
};

BulkResult check_archive_entries(const std::vector<EntryInfo>& entries,
                                 const std::filesystem::path& destination_root);

// ---------------------------------------------------------------------------
//  Pure path policy: returns Ok or the verdict for the path string
//  alone, without consulting any attributes or the filesystem. Exposed
//  so the conformance table (zip-entry-paths.json) can be re-used by
//  tests without standing up an actual ZIP.
// ---------------------------------------------------------------------------

Verdict classify_path(const std::string& entry_name);

}  // namespace arrow::skin
