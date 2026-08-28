// SPDX-License-Identifier: MPL-2.0
//
// zip_safety.cpp — see zip_safety.hpp for the rules being enforced.

#include "skin/zip_safety.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>

namespace arrow::skin {

namespace {

// Split a path into components on '/'. Empty components (from leading
// or trailing slashes, or from `//`) are skipped — the ZIP spec says
// paths are forward-slash separated, and an empty component is
// already a smell we want to look at separately (see below).
std::vector<std::string> split_path(const std::string& p) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : p) {
        if (c == '/') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

Verdict classify_path(const std::string& entry_name) {
    if (entry_name.empty()) return Verdict::EmptyPath;
    if (entry_name.size() > kMaxPathLength) return Verdict::PathTooLong;

    // Reject drive letters and UNC paths. Both spellings show up in
    // crafted archives: "C:\..." and "C:/..." in payload tests, and
    // "\\server\share\..." for the UNC variant.
    if (entry_name.size() >= 2 && std::isalpha(static_cast<unsigned char>(entry_name[0])) &&
        (entry_name[1] == ':')) {
        return Verdict::AbsolutePath;
    }
    if (starts_with(entry_name, "\\\\") || starts_with(entry_name, "//")) {
        return Verdict::AbsolutePath;
    }
    if (entry_name.front() == '/') return Verdict::AbsolutePath;

    // Backslash as a path separator: not portable, not in any of the
    // allow-list patterns, and a classic escape vector.
    if (entry_name.find('\\') != std::string::npos) return Verdict::BackslashSeparator;

    // Walk the components.
    auto parts = split_path(entry_name);
    if (parts.empty()) return Verdict::EmptyPath;  // all slashes

    for (const auto& part : parts) {
        if (part == "." || part == "..") return Verdict::DotDotSegment;
        for (unsigned char c : part) {
            if (c == 0)  return Verdict::NullByte;
            if (c < 0x20 || c == 0x7f) return Verdict::ControlChar;
        }
    }

    if (parts.size() > kMaxPathDepth) return Verdict::DepthExceeded;

    // The first component must be one of the permitted top-level
    // entries. We compare against the table in zip_safety.hpp; an
    // entry is "permitted" if its first path component is in
    // kPermittedTopLevel verbatim, OR if it is one of the three
    // top-level files (theme.json, LICENSE, preview.png) that the
    // spec lists as a flat member.
    const std::string& top = parts.front();
    bool found = false;
    for (auto allowed : kPermittedTopLevel) {
        // The kPermittedTopLevel entries ending in "/" are directory
        // names; the rest are flat filenames. We compare accordingly.
        if (!allowed.empty() && allowed.back() == '/') {
            if (top == std::string{allowed.substr(0, allowed.size() - 1)}) {
                found = true;
                break;
            }
        } else {
            if (top == std::string{allowed}) {
                found = true;
                break;
            }
        }
    }
    if (!found) return Verdict::UnlistedTopLevel;

    return Verdict::Ok;
}

// ---------------------------------------------------------------------------
//  check_entry_path — the full check, including the resolved-path test
//  and the file-kind policy.
// ---------------------------------------------------------------------------

SafetyResult check_entry_path(const std::string& entry_name,
                              const EntryAttributes& attr,
                              const std::filesystem::path& destination_root) {
    auto reason = [](Verdict v, const std::string& s) {
        return SafetyResult::reject(v, s);
    };

    // 1) Kind of entry. Symlinks, hard links and device nodes are
    //    refused outright; they have no business being inside a skin
    //    package, and an attacker who manages to put one in there
    //    is up to something.
    if (attr.is_symlink)   return reason(Verdict::SymlinkEntry,   "symlink entries are forbidden");
    if (attr.is_hardlink)  return reason(Verdict::HardLinkEntry,  "hard-link entries are forbidden");
    if (attr.is_device)    return reason(Verdict::DeviceNode,    "device nodes are forbidden");

    // 2) Path string itself. classify_path is the single source of
    //    truth for the textual rules; the check below is only the
    //    filesystem-side assertion.
    const Verdict v = classify_path(entry_name);
    if (v != Verdict::Ok) {
        return reason(v, "path '" + entry_name + "' is rejected by the package path policy");
    }

    // 3) Resolved destination must lie inside the install root.
    //    We canonicalise the destination root once; canonicalising the
    //    candidate path requires it to exist, but we want to check
    //    BEFORE we extract, so we instead compose the path and walk it
    //    component-by-component. Lexical normalisation of the result
    //    is good enough because every component has already been
    //    validated against the denylist above.
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonical_root = fs::weakly_canonical(destination_root, ec);
    if (ec) canonical_root = fs::absolute(destination_root, ec);
    auto candidate = canonical_root / entry_name;
    auto normalised = candidate.lexically_normal();

    // To assert "inside the root" we compare the canonicalised root
    // with the normalised candidate's prefix. We rely on the lexical
    // form here because we have not yet created the file; the deeper
    // check (after extraction) is the responsibility of the install
    // step, which re-runs this assertion on the actual filesystem
    // state.
    auto root_str  = canonical_root.generic_string();
    auto cand_str  = normalised.generic_string();
    if (cand_str.size() <= root_str.size() ||
        cand_str.compare(0, root_str.size(), root_str) != 0 ||
        (cand_str[root_str.size()] != '/' && cand_str.size() != root_str.size())) {
        return reason(Verdict::EscapesDestination,
            "resolved path '" + cand_str + "' is outside the install root");
    }
    return SafetyResult::accept();
}

// ---------------------------------------------------------------------------
//  Bulk check
// ---------------------------------------------------------------------------

BulkResult check_archive_entries(const std::vector<EntryInfo>& entries,
                                 const std::filesystem::path& destination_root) {
    BulkResult out;
    out.per_entry.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto r = check_entry_path(entries[i].name, entries[i].attr, destination_root);
        if (!r.ok()) {
            out.all_ok = false;
            if (out.first_fail == static_cast<std::size_t>(-1)) {
                out.first_fail = i;
            }
        }
        out.per_entry.push_back(std::move(r));
    }
    return out;
}

}  // namespace arrow::skin
