// SPDX-License-Identifier: MPL-2.0
//
// chain.hpp — the active theme chain.
//
// Spec: eclipse-player.md §11.2 (REQ-THM-011).
//
// A "chain" is the resolved theme that the rest of the app reads from. It
// is built from the active built-in (Dark or Light according to `mode`),
// any user-overrides, and the user-selected theme on top. The result is a
// single Theme-like value where every std::optional is filled in.
//
// The chain is cached: the spec calls for hot-reload (REQ-THM-051) and
// skin cross-fade (REQ-THM-050), both of which thrash the active theme.
// Rebuilding from scratch on every paint is not acceptable; we rebuild
// when the inputs change and hand out shared_ptr<const> handles in
// between. The cache key is a fingerprint of the input set, not the
// pointer identity, because the inputs are owned elsewhere.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "theme/loader.hpp"

namespace arrow::theme {

// ---------------------------------------------------------------------------
//  A "ThemeOverride" is a single user override file. The override file is
//  itself a theme.json that may be partial: only the fields it sets win,
//  the rest inherit from below. An override is identified by a path on
//  disk and an opaque handle — the handle is what callers pass back in
//  when they want the override removed or replaced.
// ---------------------------------------------------------------------------

struct ThemeOverride {
    std::string                   handle;     // opaque, supplied by caller
    std::filesystem::path         path;       // file on disk
    std::shared_ptr<const Theme>  parsed;     // the validated theme
};

// ---------------------------------------------------------------------------
//  ThemeChainBuilder
// ---------------------------------------------------------------------------

class ThemeChainBuilder {
public:
    ThemeChainBuilder();
    ~ThemeChainBuilder();

    ThemeChainBuilder(const ThemeChainBuilder&)            = delete;
    ThemeChainBuilder& operator=(const ThemeChainBuilder&) = delete;

    // Set the validator used to parse any new theme.json. The validator
    // outlives the chain builder.
    void set_validator(const SchemaValidator* v) { validator_ = v; }

    // Set the directory the built-in themes live in. The chain reads
    // light/theme.json, dark/theme.json, amoled/theme.json and
    // high-contrast/theme.json from this directory. The four files are
    // REQ-THM-070 — they are shipped inside the binary, but the chain
    // reads them from disk so a developer-mode install can override
    // them without rebuilding.
    void set_builtin_dir(const std::filesystem::path& dir);

    // The user-selected theme. `theme_id` is matched first against the
    // built-in themes and then against any installed third-party themes
    // (those are loaded separately by the skin installer — that part
    // is out of scope for this header, but the chain accepts a parsed
    // Theme via set_user_theme so the skin layer can hand one in).
    //
    // Pass nullptr to clear the user selection; the chain then resolves
    // to the active built-in only.
    void set_user_theme(std::shared_ptr<const Theme> theme);

    // Add or replace a user override. If a previous override with the
    // same handle existed it is replaced; otherwise the new override
    // is appended to the end of the override stack.
    void upsert_override(ThemeOverride override);

    // Remove an override by handle. No-op if the handle is not present.
    void remove_override(const std::string& handle);

    // The mode the chain should resolve to when no user selection is
    // active. "light" or "dark"; the chain maps it to the matching
    // built-in. The chain does not auto-detect system mode; that is
    // the caller's job (the UI knows about the OS setting).
    void set_default_mode(const std::string& mode);

    // Build the chain. The result is cached: a call that produces the
    // same inputs as the previous call returns the same shared_ptr
    // without re-parsing or re-resolving. The cache is invalidated
    // automatically by every mutator above.
    std::shared_ptr<const Theme> build();

    // For tests: bypass the cache and force a rebuild.
    std::shared_ptr<const Theme> rebuild();

    // The resolved built-in id (e.g. "dark", "light", "high-contrast")
    // that the chain most recently resolved to. Useful for the UI to
    // display "Currently: High Contrast" without a second walk.
    const std::string& resolved_builtin_id() const { return resolved_id_; }

private:
    // Walk the `extends` chain and merge in any unset fields. The base
    // case is a built-in with no `extends`. Cycles in `extends` are
    // detected and reported as a configuration error.
    std::shared_ptr<Theme> resolve_extends(
        const std::shared_ptr<const Theme>& leaf,
        const std::vector<std::shared_ptr<const Theme>>& builtins);

    // The four built-in themes, loaded once and cached.
    void ensure_builtins_loaded(std::string& error_out);

    // Cache bookkeeping
    std::uint64_t compute_fingerprint() const;
    std::shared_ptr<const Theme> cached_;
    std::uint64_t cached_fingerprint_{0};

    // Inputs
    const SchemaValidator* validator_{nullptr};
    std::filesystem::path  builtin_dir_;
    std::string            default_mode_{"dark"};
    std::shared_ptr<const Theme> user_theme_;
    std::vector<ThemeOverride>   overrides_;
    std::unordered_set<std::string> override_handles_;  // for O(1) dedup

    // Built-ins, loaded on first build().
    std::vector<std::shared_ptr<const Theme>> builtins_;
    bool builtins_loaded_{false};
    std::string resolved_id_;  // last-resolved built-in id
};

}  // namespace arrow::theme
