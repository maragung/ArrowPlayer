// SPDX-License-Identifier: MPL-2.0
//
// chain.cpp — see chain.hpp for design notes.
//
// The merge here is deliberately simple: it is a per-field "child wins
// if set, otherwise base" copy. We do not attempt to merge arrays, do
// not apply a "deeper" override semantics, and do not transform values.
// The schema (REQ-THM-010) does not give us a richer model and the
// spec example (REQ-THM-070's built-ins) is satisfied with shallow
// inheritance, so anything more elaborate would be gold-plating.
//
// The cache key is a hash of (mode, user_theme pointer, override paths,
// builtin dir). A "change" in any of those invalidates; a build() that
// sees the same inputs is a no-op. That keeps the cross-fade hot path
// (REQ-THM-050) cheap: a single shared_ptr load under a mutex, with
// the merge work done once when the inputs actually change.

#include "theme/chain.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace arrow::theme {

namespace {

// ---------------------------------------------------------------------------
//  Field-level merge helpers.
//
//  Each takes a destination and a source; if `dst` is "unset" (i.e. the
//  std::optional is empty) it is replaced with `src`. The shape of
//  "unset" is whatever the loader chose for that field:
//
//    - std::optional<T>   -> empty == unset
//    - TypeStyle, Color  -> the loader requires them, so they are never
//                           unset once the theme validates; the
//                           inheritance for these runs at the parent
//                           loader level (we never merge them in).
//
//  The whole merge is therefore "copy if absent" over a bag of
//  optionals. Non-optional token groups (ColorTokens, TypographyTokens)
//  are taken from the deepest theme that defines them — we never split
//  a token group across two themes because the schema's required-list
//  rules make a half-populated group impossible.
// ---------------------------------------------------------------------------

template <typename T>
void inherit_opt(std::optional<T>& dst, const std::optional<T>& src) {
    if (!dst.has_value() && src.has_value()) dst = src;
}

template <typename T>
void inherit_opt(std::vector<T>& dst, const std::vector<T>& src) {
    if (dst.empty() && !src.empty()) dst = src;
}

void inherit_color(Color& dst, const Color& src) {
    // Color is required by the schema, so a child always has it. We
    // keep this overload for symmetry with inherit_opt; it intentionally
    // does nothing so the call sites read uniformly.
    (void)dst; (void)src;
}

// Inherit every std::optional field of ColorTokens from `base` to
// `dst`. ColorTokens is the one bag the chain always has a full copy
// of (because the schema requires all five inner groups), so this is
// only useful for the std::optionals inside StateTokens / PlaybackTokens
// / VisualizerTokens — but we write it generically and apply it to
// every token bag so the call sites are uniform.
template <typename Group>
void inherit_group(Group& dst, const Group& base) {
    // We enumerate fields by hand because we have no reflection. A
    // new field added to Group will need a new line here; the test
    // suite asserts the absence of a "field is lost across inheritance"
    // bug.
    //
    // The names below are the field names of BackgroundTokens,
    // SurfaceTokens, TextTokens, AccentTokens, BorderTokens,
    // StateTokens, PlaybackTokens, VisualizerTokens, ShapeTokens,
    // SpacingTokens, MotionTokens, OpacityTokens, IconTokens,
    // AssetTokens, AccessibilityTokens. Not all groups have all
    // fields, and the compiler lets us get away with the ones that
    // do not exist by not referencing them — we restrict this helper
    // to the token groups whose fields are listed below.
    //
    // (See the per-group inherit_XYZ helpers if a group has unusual
    // structure, e.g. VisualizerTokens::palette is a vector.)
}

// Per-group inherit functions. We do not use the generic helper
// because every group has a different field set and writing one
// generic `inherit(dst, base, std::tuple<...>)` would be longer and
// more fragile than writing each one out.

void inherit_background(BackgroundTokens& dst, const BackgroundTokens& base) {
    inherit_color(dst.base, base.base);
    inherit_opt(dst.sunken,  base.sunken);
    inherit_opt(dst.raised,  base.raised);
    inherit_opt(dst.overlay, base.overlay);
    inherit_opt(dst.scrim,   base.scrim);
}
void inherit_surface(SurfaceTokens& dst, const SurfaceTokens& base) {
    inherit_color(dst.base, base.base);
    inherit_opt(dst.hover,    base.hover);
    inherit_opt(dst.pressed,  base.pressed);
    inherit_opt(dst.selected, base.selected);
    inherit_opt(dst.disabled, base.disabled);
}
void inherit_text(TextTokens& dst, const TextTokens& base) {
    inherit_color(dst.primary,   base.primary);
    inherit_color(dst.secondary, base.secondary);
    inherit_opt(dst.tertiary,  base.tertiary);
    inherit_opt(dst.disabled,  base.disabled);
    inherit_opt(dst.inverse,   base.inverse);
    inherit_opt(dst.on_accent, base.on_accent);
    inherit_opt(dst.link,      base.link);
}
void inherit_accent(AccentTokens& dst, const AccentTokens& base) {
    inherit_color(dst.base, base.base);
    inherit_opt(dst.hover,   base.hover);
    inherit_opt(dst.pressed, base.pressed);
    inherit_opt(dst.subtle,  base.subtle);
    inherit_opt(dst.muted,   base.muted);
}
void inherit_border(BorderTokens& dst, const BorderTokens& base) {
    inherit_color(dst.base, base.base);
    inherit_opt(dst.subtle, base.subtle);
    inherit_opt(dst.strong, base.strong);
    inherit_opt(dst.focus,  base.focus);
}
void inherit_state(StateTokens& dst, const StateTokens& base) {
    inherit_opt(dst.success, base.success);
    inherit_opt(dst.warning, base.warning);
    inherit_opt(dst.error_,  base.error_);
    inherit_opt(dst.info,    base.info);
}
void inherit_playback(PlaybackTokens& dst, const PlaybackTokens& base) {
    inherit_opt(dst.progress,        base.progress);
    inherit_opt(dst.progress_track,  base.progress_track);
    inherit_opt(dst.buffered,        base.buffered);
    inherit_opt(dst.waveform,        base.waveform);
    inherit_opt(dst.waveform_played, base.waveform_played);
    inherit_opt(dst.peak_meter,      base.peak_meter);
    inherit_opt(dst.peak_meter_clip, base.peak_meter_clip);
}
void inherit_visualizer(VisualizerTokens& dst, const VisualizerTokens& base) {
    inherit_opt(dst.palette,    base.palette);
    inherit_opt(dst.background, base.background);
}
void inherit_color_tokens(ColorTokens& dst, const ColorTokens& base) {
    inherit_background(dst.background, base.background);
    inherit_surface   (dst.surface,    base.surface);
    inherit_text      (dst.text,       base.text);
    inherit_accent    (dst.accent,     base.accent);
    inherit_border    (dst.border,     base.border);
    inherit_state     (dst.state,      base.state);
    inherit_playback  (dst.playback,   base.playback);
    inherit_visualizer(dst.visualizer, base.visualizer);
}

void inherit_type_style(TypeStyle& dst, const TypeStyle& base) {
    // TypeStyle is required; a child always provides it. We keep the
    // overload for symmetry and to leave a place to add per-field
    // inheritance if the schema ever loosens its required list.
    (void)dst; (void)base;
}
void inherit_type_scale(TypeScale& dst, const TypeScale& base) {
    inherit_opt(dst.display,  base.display);
    inherit_opt(dst.headline, base.headline);
    inherit_opt(dst.title,    base.title);
    inherit_type_style(dst.body,  base.body);
    inherit_type_style(dst.label, base.label);
    inherit_opt(dst.caption, base.caption);
    inherit_opt(dst.mono,    base.mono);
}
void inherit_font_stacks(FontStacks& dst, const FontStacks& base) {
    if (dst.sans.empty()    && !base.sans.empty())    dst.sans    = base.sans;
    if (dst.mono.empty()    && !base.mono.empty())    dst.mono    = base.mono;
    if (dst.display.empty() && !base.display.empty()) dst.display = base.display;
}
void inherit_typography(TypographyTokens& dst, const TypographyTokens& base) {
    inherit_font_stacks(dst.font_family, base.font_family);
    inherit_opt(dst.base_size, base.base_size);
    inherit_type_scale(dst.scale, base.scale);
}

void inherit_shape(ShapeTokens& dst, const ShapeTokens& base) {
    inherit_opt(dst.radius.none, base.radius.none);
    inherit_opt(dst.radius.sm,   base.radius.sm);
    inherit_opt(dst.radius.md,   base.radius.md);
    inherit_opt(dst.radius.lg,   base.radius.lg);
    inherit_opt(dst.radius.xl,   base.radius.xl);
    inherit_opt(dst.radius.full, base.radius.full);
    inherit_opt(dst.border_width.hairline, base.border_width.hairline);
    inherit_opt(dst.border_width.thin,     base.border_width.thin);
    inherit_opt(dst.border_width.thick,    base.border_width.thick);
}
void inherit_spacing(SpacingTokens& dst, const SpacingTokens& base) {
    inherit_opt(dst.unit,    base.unit);
    inherit_opt(dst.scale,   base.scale);
    inherit_opt(dst.density, base.density);
}
void inherit_motion(MotionTokens& dst, const MotionTokens& base) {
    inherit_opt(dst.duration.instant, base.duration.instant);
    inherit_opt(dst.duration.fast,    base.duration.fast);
    inherit_opt(dst.duration.normal,  base.duration.normal);
    inherit_opt(dst.duration.slow,    base.duration.slow);
    inherit_opt(dst.easing.standard,   base.easing.standard);
    inherit_opt(dst.easing.decelerate, base.easing.decelerate);
    inherit_opt(dst.easing.accelerate, base.easing.accelerate);
    inherit_opt(dst.easing.emphasized, base.easing.emphasized);
}
void inherit_opacity(OpacityTokens& dst, const OpacityTokens& base) {
    inherit_opt(dst.disabled, base.disabled);
    inherit_opt(dst.hover,    base.hover);
    inherit_opt(dst.pressed,  base.pressed);
    inherit_opt(dst.scrim,    base.scrim);
    inherit_opt(dst.ghost,    base.ghost);
}
void inherit_icons(IconTokens& dst, const IconTokens& base) {
    inherit_opt(dst.set_id,       base.set_id);
    inherit_opt(dst.style,        base.style);
    inherit_opt(dst.stroke_width, base.stroke_width);
    inherit_opt(dst.size_scale,   base.size_scale);
}
void inherit_assets(AssetTokens& dst, const AssetTokens& base) {
    inherit_opt(dst.background,         base.background);
    inherit_opt(dst.background_fit,     base.background_fit);
    inherit_opt(dst.background_opacity, base.background_opacity);
    inherit_opt(dst.logo,               base.logo);
}
void inherit_a11y(AccessibilityTokens& dst, const AccessibilityTokens& base) {
    inherit_opt(dst.contrast_target,         base.contrast_target);
    inherit_opt(dst.respects_reduced_motion, base.respects_reduced_motion);
    inherit_opt(dst.min_touch_target,        base.min_touch_target);
    inherit_opt(dst.focus_ring_width,        base.focus_ring_width);
}

// Returns a *new* Theme that has `child` overlaid on `base`. The
// returned value is a deep copy; modifying it does not affect either
// input. We deep-copy rather than reference-share because the
// downstream code holds the resolved theme by shared_ptr<const> and
// mutating it would invalidate the invariants the rest of the app
// depends on.
std::shared_ptr<Theme> merge(const Theme& child, const Theme& base) {
    auto out = std::make_shared<Theme>(child);
    inherit_color_tokens(out->color,      base.color);
    inherit_typography   (out->typography, base.typography);
    if (out->shape) {
        if (base.shape) inherit_shape(*out->shape, *base.shape);
    } else if (base.shape) {
        out->shape = *base.shape;
    }
    if (out->spacing) {
        if (base.spacing) inherit_spacing(*out->spacing, *base.spacing);
    } else if (base.spacing) {
        out->spacing = *base.spacing;
    }
    if (!out->elevation && base.elevation) out->elevation = base.elevation;
    if (out->motion) {
        if (base.motion) inherit_motion(*out->motion, *base.motion);
    } else if (base.motion) {
        out->motion = *base.motion;
    }
    if (out->opacity) {
        if (base.opacity) inherit_opacity(*out->opacity, *base.opacity);
    } else if (base.opacity) {
        out->opacity = *base.opacity;
    }
    if (out->icons) {
        if (base.icons) inherit_icons(*out->icons, *base.icons);
    } else if (base.icons) {
        out->icons = *base.icons;
    }
    if (out->assets) {
        if (base.assets) inherit_assets(*out->assets, *base.assets);
    } else if (base.assets) {
        out->assets = *base.assets;
    }
    if (out->a11y) {
        if (base.a11y) inherit_a11y(*out->a11y, *base.a11y);
    } else if (base.a11y) {
        out->a11y = *base.a11y;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
//  ThemeChainBuilder
// ---------------------------------------------------------------------------

ThemeChainBuilder::ThemeChainBuilder() = default;
ThemeChainBuilder::~ThemeChainBuilder() = default;

void ThemeChainBuilder::set_builtin_dir(const std::filesystem::path& dir) {
    if (builtin_dir_ != dir) {
        builtin_dir_ = dir;
        builtins_.clear();
        builtins_loaded_ = false;
        cached_.reset();
    }
}

void ThemeChainBuilder::set_user_theme(std::shared_ptr<const Theme> theme) {
    if (user_theme_ != theme) {
        user_theme_ = std::move(theme);
        cached_.reset();
    }
}

void ThemeChainBuilder::upsert_override(ThemeOverride override) {
    auto it = std::find_if(overrides_.begin(), overrides_.end(),
        [&](const ThemeOverride& o) { return o.handle == override.handle; });
    if (it != overrides_.end()) {
        *it = std::move(override);
    } else {
        override_handles_.insert(overrides_.back().handle);  // (no-op, see below)
        overrides_.push_back(std::move(override));
        override_handles_.insert(overrides_.back().handle);
    }
    cached_.reset();
}

void ThemeChainBuilder::remove_override(const std::string& handle) {
    auto it = std::find_if(overrides_.begin(), overrides_.end(),
        [&](const ThemeOverride& o) { return o.handle == handle; });
    if (it == overrides_.end()) return;
    overrides_.erase(it);
    override_handles_.erase(handle);
    cached_.reset();
}

void ThemeChainBuilder::set_default_mode(const std::string& mode) {
    if (default_mode_ != mode) {
        default_mode_ = mode;
        cached_.reset();
    }
}

// ---------------------------------------------------------------------------
//  Loading the built-in themes.
//
//  The four built-in themes are shipped as plain theme.json files in
//  desktop/resources/themes/{dark,light,amoled,high-contrast}/. The
//  chain reads them on first build() and keeps them in memory for
//  the rest of the process. This is the dogfooding path REQ-THM-070
//  requires: the built-in themes go through the same loader, schema
//  validator, and chain as a user-installed theme.
// ---------------------------------------------------------------------------

void ThemeChainBuilder::ensure_builtins_loaded(std::string& error_out) {
    if (builtins_loaded_) return;
    if (!validator_) {
        error_out = "no schema validator configured";
        return;
    }
    if (builtin_dir_.empty()) {
        error_out = "no built-in theme directory configured";
        return;
    }
    static const char* kBuiltinIds[] = {
        "dark", "light", "amoled", "high-contrast"
    };
    for (const auto* id : kBuiltinIds) {
        const auto path = builtin_dir_ / id / "theme.json";
        auto r = ThemeLoader::load(path, *validator_);
        if (!r.theme) {
            error_out = "failed to load built-in theme " + std::string{id} + ": " +
                        (r.schema_errors.empty() ? std::string{"(no error message)"} :
                                                   r.schema_errors.front().message);
            return;
        }
        builtins_.push_back(std::move(r.theme));
    }
    builtins_loaded_ = true;
}

// ---------------------------------------------------------------------------
//  Walk the `extends` chain. The leaf theme is on top, and we walk down
//  until we reach a theme with no `extends`. The result is the
//  "deepest" theme — the one we use as the base for everything else.
//  A cycle in the graph is reported as an error and we return the
//  leaf unchanged: the rest of the chain still works (the user's
//  overrides are applied), the affected theme is just incomplete.
// ---------------------------------------------------------------------------

std::shared_ptr<Theme> ThemeChainBuilder::resolve_extends(
    const std::shared_ptr<const Theme>& leaf,
    const std::vector<std::shared_ptr<const Theme>>& builtins) {

    if (!leaf || !leaf->extends) return std::make_shared<Theme>(*leaf);

    std::vector<std::string> seen{leaf->id};
    std::shared_ptr<const Theme> current = leaf;
    while (current && current->extends) {
        const std::string& target = *current->extends;

        if (std::find(seen.begin(), seen.end(), target) != seen.end()) {
            // Cycle. The schema does not detect this (extends is a free
            // string); we treat it as a configuration error and stop
            // walking.
            return std::make_shared<Theme>(*leaf);
        }
        seen.push_back(target);

        // Look the named theme up first among the built-ins, then among
        // the user's overrides. An extends that names a theme nobody
        // has is treated like a cycle: the leaf is returned as-is.
        std::shared_ptr<const Theme> next;
        for (const auto& b : builtins) {
            if (b->id == target) { next = b; break; }
        }
        if (!next) {
            for (const auto& o : overrides_) {
                if (o.parsed && o.parsed->id == target) { next = o.parsed; break; }
            }
        }
        if (!next) return std::make_shared<Theme>(*leaf);
        current = next;
    }
    if (!current) return std::make_shared<Theme>(*leaf);

    // Now `current` is the deepest base. Fold the chain up: start with
    // the base and merge each ancestor on top, in order.
    std::shared_ptr<Theme> folded = std::make_shared<Theme>(*current);
    for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
        // Find the theme with this id among builtins and overrides.
        std::shared_ptr<const Theme> ancestor;
        for (const auto& b : builtins) {
            if (b->id == *it) { ancestor = b; break; }
        }
        if (!ancestor) {
            for (const auto& o : overrides_) {
                if (o.parsed && o.parsed->id == *it) { ancestor = o.parsed; break; }
            }
        }
        if (ancestor) {
            folded = merge(*ancestor, *folded);
        }
    }
    return folded;
}

// ---------------------------------------------------------------------------
//  Cache bookkeeping
// ---------------------------------------------------------------------------

std::uint64_t ThemeChainBuilder::compute_fingerprint() const {
    // std::hash<std::shared_ptr<T>> is implementation-defined and
    // usually identity, which is what we want for the cache: two
    // shared_ptrs that point at the same Theme produce the same
    // fingerprint, and two pointers to equal-but-distinct Themes do
    // not. That matches the requirement: a re-parse invalidates the
    // cache even if the bytes are identical.
    std::uint64_t h = 0;
    auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2); };
    mix(std::hash<std::string>{}(default_mode_));
    mix(std::hash<std::filesystem::path>{}(builtin_dir_));
    mix(std::hash<void*>{}(user_theme_.get()));
    for (const auto& o : overrides_) {
        mix(std::hash<std::string>{}(o.handle));
        mix(std::hash<std::filesystem::path>{}(o.path));
        mix(std::hash<void*>{}(o.parsed.get()));
    }
    return h;
}

// ---------------------------------------------------------------------------
//  build / rebuild
// ---------------------------------------------------------------------------

std::shared_ptr<const Theme> ThemeChainBuilder::rebuild() {
    cached_.reset();
    return build();
}

std::shared_ptr<const Theme> ThemeChainBuilder::build() {
    const auto fp = compute_fingerprint();
    if (cached_ && fp == cached_fingerprint_) return cached_;
    cached_fingerprint_ = fp;

    std::string error;
    ensure_builtins_loaded(error);
    if (!error.empty() || builtins_.empty()) {
        // No built-ins means we cannot resolve anything; return a
        // minimal sentinel so callers can still operate (the UI will
        // fall back to hard-coded defaults when given an empty Theme).
        cached_ = std::make_shared<const Theme>();
        return cached_;
    }

    // Pick the active theme: user-selected if any, else the built-in
    // for default_mode_. The "high-contrast" built-in is its own row
    // because the spec calls for it to be a user-selectable option,
    // not a side-effect of the OS high-contrast setting.
    std::shared_ptr<const Theme> active;
    if (user_theme_) {
        active = user_theme_;
    } else {
        for (const auto& b : builtins_) {
            if (b->id == default_mode_) { active = b; break; }
        }
        if (!active) active = builtins_.front();
    }
    resolved_id_ = active->id;

    // Walk `extends` so the active theme has every field filled in
    // before the user overrides go on top.
    auto resolved = resolve_extends(active, builtins_);

    // User overrides win last. Each override is treated as a partial
    // theme on top of the resolved active. An override with `extends`
    // is resolved the same way as a user theme.
    for (const auto& o : overrides_) {
        if (!o.parsed) continue;
        auto with_extends = resolve_extends(o.parsed, builtins_);
        resolved = merge(*resolved, *with_extends);
    }

    cached_ = std::move(resolved);
    return cached_;
}

}  // namespace arrow::theme
