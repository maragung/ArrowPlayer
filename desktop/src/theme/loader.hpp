// SPDX-License-Identifier: MPL-2.0
//
// loader.hpp — Theme object and on-disk loader.
//
// Spec: eclipse-player.md §11.2 (REQ-THM-010, REQ-THM-011, REQ-THM-012).
//
// A Theme is the in-memory representation of a validated theme.json.
// The data model here is intentionally flat: every field on Theme is a
// direct projection of a token in the schema. We do not invent fields,
// we do not collapse token groups into enums, and we do not pre-compute
// derived values (e.g. effective text colour) — that work belongs in
// the chain (chain.hpp), where it is observable and testable.
//
// The in-memory representation is owned via std::shared_ptr<const Theme>
// once it has been parsed and validated: the chain, the UI bindings, and
// the settings panel can all hold a reference at the same time without
// coordinating through a mutex. Mutations go through chain.hpp, which
// re-parses and re-validates; the in-place Theme is immutable.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "theme/schema.hpp"

namespace arrow::theme {

// ---------------------------------------------------------------------------
//  Primitive types
// ---------------------------------------------------------------------------

// A 6- or 8-digit hex colour as written in the schema: "#RRGGBB" or
// "#RRGGBBAA". Stored as the original string so a renderer that needs
// RGBA can do its own conversion; the Theme does not interpret colour
// values beyond verifying the schema's pattern.
struct Color {
    std::string hex;

    bool operator==(const Color& o) const noexcept { return hex == o.hex; }
    bool operator!=(const Color& o) const noexcept { return !(*this == o); }
};

// A single font family name. The schema constrains a fontFamily group
// to be an array of 1..8 such names.
struct FontFamily {
    std::string name;

    bool operator==(const FontFamily& o) const noexcept { return name == o.name; }
    bool operator!=(const FontFamily& o) const noexcept { return !(*this == o); }
};

// A type-scale token. §12.1 pins down the seven sizes; the schema
// allows extra metadata (line-height, weight, letter-spacing,
// text-transform) that the renderer may use.
struct TypeStyle {
    double            size{14.0};
    std::optional<double>         line_height;
    std::optional<std::int32_t>   weight;
    std::optional<double>         letter_spacing;
    std::optional<std::string>    transform;  // "none" | "uppercase" | ...

    bool operator==(const TypeStyle& o) const noexcept {
        return size == o.size && line_height == o.line_height &&
               weight == o.weight && letter_spacing == o.letter_spacing &&
               transform == o.transform;
    }
    bool operator!=(const TypeStyle& o) const noexcept { return !(*this == o); }
};

// A cubic-bezier control point, exactly four doubles in [-2, 2].
struct CubicBezier {
    double x1{0.0}, y1{0.0}, x2{1.0}, y2{1.0};

    bool operator==(const CubicBezier& o) const noexcept {
        return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2;
    }
};

// A single elevation step from the elevation[] ladder.
struct Elevation {
    double offset_x{0.0};
    double offset_y{0.0};
    double blur{0.0};
    double spread{0.0};
    Color  color{};
};

// ---------------------------------------------------------------------------
//  Token groups
//
//  Each group below is a struct that mirrors a sub-object in the schema.
//  Members use std::optional to mean "absent from the document" (REQ-THM-011
//  inheritance: an absent field is not the same as a null field). The
//  chain layer is responsible for walking the `extends` graph and filling
//  in any std::optional that is unset in this theme.
// ---------------------------------------------------------------------------

struct BackgroundTokens {
    Color                    base;     // required
    std::optional<Color>     sunken;
    std::optional<Color>     raised;
    std::optional<Color>     overlay;
    std::optional<Color>     scrim;
};

struct SurfaceTokens {
    Color                    base;     // required
    std::optional<Color>     hover;
    std::optional<Color>     pressed;
    std::optional<Color>     selected;
    std::optional<Color>     disabled;
};

struct TextTokens {
    Color                    primary;     // required
    Color                    secondary;   // required
    std::optional<Color>     tertiary;
    std::optional<Color>     disabled;
    std::optional<Color>     inverse;
    std::optional<Color>     on_accent;
    std::optional<Color>     link;
};

struct AccentTokens {
    Color                    base;     // required
    std::optional<Color>     hover;
    std::optional<Color>     pressed;
    std::optional<Color>     subtle;
    std::optional<Color>     muted;
};

struct BorderTokens {
    Color                    base;     // required
    std::optional<Color>     subtle;
    std::optional<Color>     strong;
    std::optional<Color>     focus;
};

struct StateTokens {
    std::optional<Color> success;
    std::optional<Color> warning;
    std::optional<Color> error_;
    std::optional<Color> info;
};

struct PlaybackTokens {
    std::optional<Color> progress;
    std::optional<Color> progress_track;
    std::optional<Color> buffered;
    std::optional<Color> waveform;
    std::optional<Color> waveform_played;
    std::optional<Color> peak_meter;
    std::optional<Color> peak_meter_clip;
};

struct VisualizerTokens {
    std::optional<std::vector<Color>> palette;
    std::optional<Color>             background;
};

struct ColorTokens {
    BackgroundTokens background;
    SurfaceTokens    surface;
    TextTokens       text;
    AccentTokens     accent;
    BorderTokens     border;
    StateTokens      state;
    PlaybackTokens   playback;
    VisualizerTokens visualizer;
};

struct FontStacks {
    std::vector<FontFamily> sans;     // required: 1..8
    std::vector<FontFamily> mono;
    std::vector<FontFamily> display;
};

struct TypeScale {
    std::optional<TypeStyle> display;
    std::optional<TypeStyle> headline;
    std::optional<TypeStyle> title;
    TypeStyle                body;    // required
    TypeStyle                label;   // required
    std::optional<TypeStyle> caption;
    std::optional<TypeStyle> mono;
};

struct TypographyTokens {
    FontStacks font_family;
    std::optional<double> base_size;   // 8..24, default 14
    TypeScale  scale;
};

struct ShapeRadius {
    std::optional<int>    none;     // const 0 in the schema
    std::optional<int>    sm;
    std::optional<int>    md;
    std::optional<int>    lg;
    std::optional<int>    xl;
    std::optional<int>    full;     // const 9999 in the schema
};

struct ShapeBorderWidth {
    std::optional<double> hairline;
    std::optional<double> thin;
    std::optional<double> thick;
};

struct ShapeTokens {
    ShapeRadius      radius;
    ShapeBorderWidth border_width;
};

struct SpacingTokens {
    std::optional<int>                       unit;       // 1..16, default 4
    std::optional<std::vector<int>>          scale;      // 4..16 items
    std::optional<std::string>               density;    // "compact" | ...
};

struct MotionDuration {
    std::optional<int> instant;
    std::optional<int> fast;
    std::optional<int> normal;
    std::optional<int> slow;
};

struct MotionEasing {
    std::optional<CubicBezier> standard;
    std::optional<CubicBezier> decelerate;
    std::optional<CubicBezier> accelerate;
    std::optional<CubicBezier> emphasized;
};

struct MotionTokens {
    MotionDuration duration;
    MotionEasing   easing;
};

struct OpacityTokens {
    std::optional<double> disabled;
    std::optional<double> hover;
    std::optional<double> pressed;
    std::optional<double> scrim;
    std::optional<double> ghost;
};

struct IconTokens {
    std::optional<std::string> set_id;        // lower-case, kebab
    std::optional<std::string> style;         // "outline" | "filled" | "duotone"
    std::optional<double>     stroke_width;   // 0.5..4
    std::optional<double>     size_scale;     // 0.5..2
};

struct AssetTokens {
    std::optional<std::string> background;       // images/... or icons/... or fonts/...
    std::optional<std::string> background_fit;   // "cover" | "contain" | ...
    std::optional<double>      background_opacity;
    std::optional<std::string> logo;
};

struct AccessibilityTokens {
    std::optional<std::string> contrast_target;  // "AA" | "AAA"
    std::optional<bool>        respects_reduced_motion;
    std::optional<int>         min_touch_target; // 24..96
    std::optional<int>         focus_ring_width; // 1..8
};

// ---------------------------------------------------------------------------
//  Theme
// ---------------------------------------------------------------------------

struct Theme {
    // Identity
    std::int32_t  schema_version{1};
    std::string   id;                // [a-z0-9-]{1,64}
    std::string   name;              // 1..64 chars
    std::optional<std::string> author;
    std::string   version;           // X.Y.Z
    std::optional<std::string> license;       // SPDX
    std::optional<std::string> homepage;
    std::optional<std::string> description;
    std::optional<std::string> min_app_version;
    std::optional<std::string> extends;       // id of another theme

    // Mode
    std::string mode;                // "light" | "dark"

    // Token groups
    ColorTokens         color;
    TypographyTokens    typography;
    std::optional<ShapeTokens>      shape;
    std::optional<SpacingTokens>    spacing;
    std::optional<std::vector<Elevation>> elevation;
    std::optional<MotionTokens>     motion;
    std::optional<OpacityTokens>    opacity;
    std::optional<IconTokens>       icons;
    std::optional<AssetTokens>      assets;
    std::optional<AccessibilityTokens> a11y;
};

// ---------------------------------------------------------------------------
//  Loader
//
//  load() is the single public entry point. It:
//    1. Reads the file at `path` as UTF-8.
//    2. Validates it against the theme schema (REQ-THM-010).
//    3. Builds a Theme object and returns it.
//
//  parse() is the same minus step 1; it is the helper tests use to feed
//  crafted documents straight from memory without writing them to disk.
// ---------------------------------------------------------------------------

class ThemeLoader {
public:
    struct LoadResult {
        std::shared_ptr<const Theme>                       theme;
        std::vector<SchemaError>                            schema_errors;
    };

    // Load and validate a theme.json from disk.
    static LoadResult load(const std::filesystem::path& path,
                           const SchemaValidator& validator);

    // Parse and validate a theme.json from memory. Used by tests and by
    // code paths that already have the bytes (e.g. reading a theme out
    // of a skin package).
    static LoadResult parse(std::string_view document,
                            const SchemaValidator& validator);
};

}  // namespace arrow::theme
