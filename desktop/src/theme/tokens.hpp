// SPDX-License-Identifier: MPL-2.0
//
// tokens.hpp — Canonical design tokens from shared-spec/design-system/tokens.json.
//
// Spec: eclipse-player.md §12.1 (REQ-UIX-001), §11.2 (REQ-THM-010).
//
// The canonical token values live in shared-spec/design-system/tokens.json.  Both
// platforms consume that file (desktop via a .qrc-embedded resource, Android via
// a build-time code generator) rather than duplicating the numbers.  This file
// is the C++ projection of those values into a strongly-typed, immutable struct
// hierarchy.
//
// Token categories
// ================
// Every token is immutable after loading.  Accessors that might compute a
// derived value are avoided: the chain layer does that, and it is observable
// and testable there.
//
// Thread-safety: TokenBundle is immutable after construction and is safe to
// share across threads via shared_ptr<const TokenBundle>.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace arrow::theme {

// ---------------------------------------------------------------------------
//  Primitive token values
// ---------------------------------------------------------------------------

/// A numeric token that carries its canonical unit.  Tokens from tokens.json
/// are all in specific units; the unit field records which one so a renderer
/// can convert if needed.
enum class TokenUnit : std::uint8_t {
    px,   ///< pixels (spacing, radii, border widths)
    sp,   ///< scale-independent pixels (typography size)
    dp,   ///< density-independent pixels (Android layout)
    ms,   ///< milliseconds (motion durations)
    s,    ///< seconds (motion durations)
    None, ///< unitless (multipliers, ratios, opacities)
};

/// A single numeric token: a name, a raw value, and its canonical unit.
struct NumericToken {
    std::string  name;
    double       value{0.0};
    TokenUnit    unit{TokenUnit::None};

    constexpr bool operator==(const NumericToken&) const noexcept = default;
};

/// A type-scale token.  §12.1 pins down seven sizes; the full schema also
/// carries line-height, weight, and letter-spacing which a renderer uses.
struct TypeToken {
    std::string  name;
    double       size{14.0};            ///< px, 6..96
    double       line_height{1.5};      ///< multiplier
    std::int32_t weight{400};           ///< 100..900
    double       letter_spacing{0.0};   ///< px, −2..8

    constexpr bool operator==(const TypeToken&) const noexcept = default;
};

/// A cubic-bezier easing curve.  All four control-point values are stored
/// even when only two are needed, so a renderer can pass them straight to
/// CSS / QML / Compose without translation.
struct EasingToken {
    std::string  name;
    double       x1{0.0};
    double       y1{0.0};
    double       x2{1.0};
    double       y2{1.0};

    constexpr bool operator==(const EasingToken&) const noexcept = default;
};

/// A motion-duration token: the name and the duration in milliseconds.
struct DurationToken {
    std::string  name;
    std::int32_t ms{250};

    constexpr bool operator==(const DurationToken&) const noexcept = default;
};

// ---------------------------------------------------------------------------
//  Token groups
// ---------------------------------------------------------------------------

/// The eight spacing tokens from §12.1.  All are expressed as integer px
/// values with the 4-px base unit baked in — no conversion needed.
struct SpacingTokens {
    std::int32_t xs{4};   ///< 4  px
    std::int32_t sm{8};   ///< 8  px
    std::int32_t md{12};  ///< 12 px
    std::int32_t lg{16};  ///< 16 px
    std::int32_t xl{24};  ///< 24 px
    std::int32_t xxl{32}; ///< 32 px
    std::int32_t xxxl{48};///< 48 px
    std::int32_t xxxxl{64};///< 64 px

    /// Return the named spacing token.  Returns nullptr if the name is unknown.
    const std::int32_t* lookup(std::string_view name) const noexcept;

    constexpr bool operator==(const SpacingTokens&) const noexcept = default;
};

/// The five elevation shadow levels from §12.1.  The shadow colour comes from
/// the active theme (never from here — a fixed shadow colour looks wrong in
/// light mode), but the geometry and alpha are canonical.
struct ElevationTokens {
    struct Level {
        std::int32_t offset_y;  ///< px
        std::int32_t blur;      ///< px
        double       alpha;      ///< 0..1
    };
    std::array<Level, 5> levels{{
        {1,  2,  0.10},
        {2,  4,  0.12},
        {4,  8,  0.14},
        {8,  16, 0.16},
        {16, 32, 0.20},
    }};

    /// Return elevation level [0..4] (caller asserts 0 ≤ level < 5).
    const Level& level(std::size_t idx) const noexcept { return levels.at(idx); }

    constexpr bool operator==(const ElevationTokens&) const noexcept = default;
};

/// The four radius tokens from §12.1.
struct RadiusTokens {
    std::int32_t sm{4};   ///< 4  px
    std::int32_t md{8};   ///< 8  px
    std::int32_t lg{12};  ///< 12 px
    std::int32_t xl{16};  ///< 16 px
    static constexpr std::int32_t full = 9999;  ///< pill shape sentinel

    /// Return the named radius token, or nullptr if unknown.
    const std::int32_t* lookup(std::string_view name) const noexcept;

    constexpr bool operator==(const RadiusTokens&) const noexcept = default;
};

/// The seven type-scale tokens from §12.1.
struct TypeScaleTokens {
    TypeToken display{ "display",  34.0, 1.15, 700, -0.5 };
    TypeToken headline{{"headline"}, 24.0, 1.25, 600, -0.25};
    TypeToken title{   "title",    18.0, 1.30, 600,  0.0 };
    TypeToken body{    "body",     14.0, 1.50, 400,  0.0 };
    TypeToken label{   "label",    13.0, 1.40, 500,  0.1 };
    TypeToken caption{ "caption",  12.0, 1.35, 400,  0.2 };
    TypeToken mono{    "mono",     13.0, 1.45, 400,  0.0 };

    /// Return the named type token, or nullptr if unknown.
    const TypeToken* lookup(std::string_view name) const noexcept;

    constexpr bool operator==(const TypeScaleTokens&) const noexcept = default;
};

/// Motion duration tokens from §12.1.
struct MotionTokens {
    DurationToken instant{"instant",  80};  ///< 80  ms — state feedback on press
    DurationToken fast{   "fast",    150};  ///< 150 ms — hover, small transitions
    DurationToken normal{"normal",   250};  ///< 250 ms — panel transitions, skin cross-fade
    DurationToken slow{   "slow",    400};  ///< 400 ms — full-screen transitions

    /// Return the named duration token, or nullptr if unknown.
    const DurationToken* lookup(std::string_view name) const noexcept;

    constexpr bool operator==(const MotionTokens&) const noexcept = default;
};

/// Motion easing curves from §12.1.
struct EasingTokens {
    EasingToken standard{
        "standard",
        0.2, 0.0, 0.0, 1.0,
    };
    EasingToken decelerate{
        "decelerate",
        0.0, 0.0, 0.2, 1.0,
    };
    EasingToken accelerate{
        "accelerate",
        0.4, 0.0, 1.0, 1.0,
    };
    /// The "emphasized" easing shares the standard curve; what makes it
    /// emphasized is that it is paired with the `slow` duration (§12.1).
    EasingToken emphasized{
        "emphasized",
        0.2, 0.0, 0.0, 1.0,
    };

    /// Return the named easing token, or nullptr if unknown.
    const EasingToken* lookup(std::string_view name) const noexcept;

    constexpr bool operator==(const EasingTokens&) const noexcept = default;
};

// ---------------------------------------------------------------------------
//  TokenBundle
//
//  The canonical design token values.  Loaded once from tokens.json at
//  startup and shared immutably.  All tokens are values only — no logic,
//  no lookups beyond the simple name→value tables above.
// ---------------------------------------------------------------------------

class TokenBundle {
public:
    TokenBundle() = default;

    /// Load from the JSON bytes of tokens.json.  Returns true on success;
    /// on failure `errors` is populated with a human-readable description.
    bool load(std::string_view json, std::vector<std::string>* errors) noexcept;

    /// Load from a file path.  Returns true on success.
    bool load(const std::filesystem::path& path,
              std::vector<std::string>* errors) noexcept;

    // Token groups — all values are canonical, never null
    const SpacingTokens&  spacing   const noexcept { return spacing_; }
    const TypeScaleTokens& typography const noexcept { return typography_; }
    const RadiusTokens&    radius    const noexcept { return radius_; }
    const ElevationTokens&  elevation const noexcept { return elevation_; }
    const MotionTokens&    motion    const noexcept { return motion_; }
    const EasingTokens&    easing   const noexcept { return easing_; }

    /// The base spacing unit (default: 4 px).
    std::int32_t spacing_unit() const noexcept { return spacing_unit_; }

private:
    SpacingTokens   spacing_;
    TypeScaleTokens typography_;
    RadiusTokens    radius_;
    ElevationTokens elevation_;
    MotionTokens    motion_;
    EasingTokens    easing_;
    std::int32_t   spacing_unit_{4};  ///< the base unit in px
};

// ---------------------------------------------------------------------------
//  Convenience: look up a spacing token by name
// ---------------------------------------------------------------------------

inline const std::int32_t* SpacingTokens::lookup(std::string_view name) const noexcept {
    if (name == "xs")  return &xs;
    if (name == "sm")  return &sm;
    if (name == "md")  return &md;
    if (name == "lg")  return &lg;
    if (name == "xl")  return &xl;
    if (name == "2xl") return &xxl;
    if (name == "3xl") return &xxxl;
    if (name == "4xl") return &xxxxl;
    return nullptr;
}

inline const std::int32_t* RadiusTokens::lookup(std::string_view name) const noexcept {
    if (name == "none") return nullptr;       // null radius
    if (name == "sm")   return &sm;
    if (name == "md")   return &md;
    if (name == "lg")   return &lg;
    if (name == "xl")   return &xl;
    if (name == "full") return &full;
    return nullptr;
}

inline const TypeToken* TypeScaleTokens::lookup(std::string_view name) const noexcept {
    if (name == "display")  return &display;
    if (name == "headline") return &headline;
    if (name == "title")    return &title;
    if (name == "body")     return &body;
    if (name == "label")    return &label;
    if (name == "caption")  return &caption;
    if (name == "mono")     return &mono;
    return nullptr;
}

inline const DurationToken* MotionTokens::lookup(std::string_view name) const noexcept {
    if (name == "instant") return &instant;
    if (name == "fast")    return &fast;
    if (name == "normal")  return &normal;
    if (name == "slow")    return &slow;
    return nullptr;
}

inline const EasingToken* EasingTokens::lookup(std::string_view name) const noexcept {
    if (name == "standard")    return &standard;
    if (name == "decelerate")  return &decelerate;
    if (name == "accelerate")  return &accelerate;
    if (name == "emphasized")  return &emphasized;
    return nullptr;
}

}  // namespace arrow::theme
