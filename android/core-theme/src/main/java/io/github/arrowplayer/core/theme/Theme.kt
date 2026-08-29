// SPDX-License-Identifier: MPL-2.0
//
// Theme.kt — Complete theme model for Android.
//
// Spec: eclipse-player.md §11.2 (REQ-THM-010 … REQ-THM-012), §11.6.
//
// This file is the Kotlin equivalent of desktop/src/theme/loader.hpp.  Both
// are initialised from the same theme.json; the schema (shared-spec/schemas/
// theme-schema.json) is the single source of truth for the token hierarchy.
//
// Thread-safety: [Theme] is immutable after construction.  All fields are
// `val` except where a default must be synthesised (e.g. missing optionals).
//
// Hilt: [Theme] is produced by [SkinLoader] and injected as a [Singleton]
// via [ThemeManager].

package io.github.arrowplayer.core.theme

// ---------------------------------------------------------------------------
//  Mode
// ---------------------------------------------------------------------------

/**
 * The theme's colour mode: light or dark.
 *
 * [Dark][Mode.Dark] is the application default.  The UI shell reads this
 * from the active theme and applies the matching Compose colour scheme.
 */
enum class Mode {
    Light,
    Dark,
}

// ---------------------------------------------------------------------------
//  Typography tokens
// ---------------------------------------------------------------------------

/**
 * A font stack: an ordered list of family names, tried left-to-right.
 *
 * This mirrors the schema's `fontStack` type.  The first available family
 * is used at render time.
 *
 * @property families Ordered list of font family names (e.g. ["Inter", "Roboto", "sans-serif"]).
 */
@JvmInline
value class FontStack(val families: List<String>) {
    init {
        require(families.isNotEmpty()) { "FontStack must have at least one family" }
        require(families.size <= 8) { "FontStack may have at most 8 families" }
    }

    /** Return the preferred (first) family name. */
    val primary: String get() = families.first()
}

/**
 * A type style: the complete specification of a text appearance.
 *
 * Mirrors the schema's `typeStyle` definition.
 *
 * @property size        Font size in px (required, 6–96).
 * @property lineHeight  Line height as a multiplier (optional, default 1.5).
 * @property weight      Font weight 100–900 (optional, default 400).
 * @property letterSpacing Letter spacing in px (optional, default 0).
 */
data class TypeStyle(
    val size: Double,
    val lineHeight: Double? = null,
    val weight: Int? = null,
    val letterSpacing: Double? = null,
)

/**
 * Typography tokens: font families and the type scale.
 *
 * @property fontFamily Sans, mono, and display font stacks.
 * @property baseSize  Base font size in px (default 14, range 8–24).
 * @property scale     Seven named type styles.
 */
data class TypographyTokens(
    val fontFamily: FontStacks = FontStacks(),
    val baseSize: Double? = null,
    val scale: TypeScale = TypeScale(),
)

data class FontStacks(
    val sans:     FontStack = FontStack(listOf("sans-serif")),
    val mono:     FontStack = FontStack(listOf("monospace")),
    val display:  FontStack = FontStack(listOf("sans-serif")),
)

/**
 * The seven named type scale steps.
 */
data class TypeScale(
    val display:  TypeStyle? = null,
    val headline: TypeStyle? = null,
    val title:    TypeStyle? = null,
    val body:     TypeStyle = TypeStyle(size = 14.0),
    val label:    TypeStyle = TypeStyle(size = 13.0),
    val caption:  TypeStyle? = null,
    val mono:     TypeStyle? = null,
)

// ---------------------------------------------------------------------------
//  Shape tokens
// ---------------------------------------------------------------------------

/**
 * Shape token groups: radii and border widths.
 *
 * @property radius      Named radius steps in dp.
 * @property borderWidth Named border width steps in dp.
 */
data class ShapeTokens(
    val radius:      RadiusTokens = RadiusTokens(),
    val borderWidth: BorderWidthTokens = BorderWidthTokens(),
)

data class RadiusTokens(
    val none: Int? = 0,
    val sm:   Int? = 4,
    val md:   Int? = 8,
    val lg:   Int? = 12,
    val xl:   Int? = 16,
    val full: Int = 9999,
)

data class BorderWidthTokens(
    val hairline: Double? = null,
    val thin:     Double? = null,
    val thick:    Double? = null,
)

// ---------------------------------------------------------------------------
//  Spacing tokens
// ---------------------------------------------------------------------------

/**
 * Spacing token groups.
 *
 * @property unit   Base spacing unit in dp (default 4, range 1–16).
 * @property scale Named spacing steps.
 * @property density Density preset: "compact", "comfortable", or "spacious".
 */
data class SpacingTokens(
    val unit:    Int? = 4,
    val scale:   List<Int>? = null,
    val density: String? = null,
)

// ---------------------------------------------------------------------------
//  Motion tokens
// ---------------------------------------------------------------------------

/**
 * Motion tokens: durations in ms and easing curves.
 *
 * @property duration  Named duration steps in ms.
 * @property easing    Named easing curves as cubic-bezier control points.
 */
data class MotionTokens(
    val duration: DurationTokens = DurationTokens(),
    val easing:   EasingTokens = EasingTokens(),
)

data class DurationTokens(
    val instant: Int? = 80,
    val fast:   Int? = 150,
    val normal: Int? = 250,
    val slow:   Int? = 400,
)

data class EasingTokens(
    val standard:    CubicBezier? = null,
    val decelerate: CubicBezier? = null,
    val accelerate: CubicBezier? = null,
    val emphasized: CubicBezier? = null,
)

// ---------------------------------------------------------------------------
//  Opacity tokens
// ---------------------------------------------------------------------------

/**
 * Opacity token groups for state-dependent element visibility.
 *
 * All values are unit intervals (0..1).
 */
data class OpacityTokens(
    val disabled: Double? = null,
    val hover:    Double? = null,
    val pressed:  Double? = null,
    val scrim:    Double? = null,
    val ghost:    Double? = null,
)

// ---------------------------------------------------------------------------
//  Icon tokens
// ---------------------------------------------------------------------------

/**
 * Icon token groups for the active icon set.
 *
 * @property setId       Icon set identifier (lower-case kebab).
 * @property style      "outline" | "filled" | "duotone".
 * @property strokeWidth Stroke width in dp (0.5–4).
 * @property sizeScale   Scale multiplier for icon sizes (0.5–2).
 */
data class IconTokens(
    val setId:       String? = null,
    val style:        String? = null,
    val strokeWidth: Double? = null,
    val sizeScale:   Double? = null,
)

// ---------------------------------------------------------------------------
//  Asset tokens
// ---------------------------------------------------------------------------

/**
 * Asset token groups: background image and logo.
 *
 * @property background      Package-relative path: "images/..." or "icons/...".
 * @property backgroundFit  How to fit the background: "cover" | "contain" | ...
 * @property backgroundOpacity Opacity of the background image (0..1).
 * @property logo           Package-relative path to a logo image.
 */
data class AssetTokens(
    val background:          String? = null,
    val backgroundFit:       String? = null,
    val backgroundOpacity:   Double? = null,
    val logo:               String? = null,
)

// ---------------------------------------------------------------------------
//  Accessibility tokens
// ---------------------------------------------------------------------------

/**
 * Accessibility token groups.
 *
 * @property contrastTarget       WCAG level: "AA" (default) or "AAA".
 * @property respectsReducedMotion Honor the OS reduced-motion setting (default true).
 * @property minTouchTarget      Minimum touch target size in dp (default 44, range 24–96).
 * @property focusRingWidth      Focus ring width in dp (default 2, range 1–8).
 */
data class AccessibilityTokens(
    val contrastTarget:         String? = "AA",
    val respectsReducedMotion: Boolean? = true,
    val minTouchTarget:        Int? = 44,
    val focusRingWidth:        Int? = 2,
)

// ---------------------------------------------------------------------------
//  The Theme
// ---------------------------------------------------------------------------

/**
 * A complete Eclipse Player theme.
 *
 * This is the Android equivalent of the C++ [arrow::theme::Theme] struct.
 * Both are loaded from the same theme.json validated against the same schema.
 *
 * All fields are `val` (immutable).  The theme chain (which resolves
 * inheritance and applies user overrides) is handled by [ThemeManager],
 * which produces a fully-resolved [Theme] where every optional field
 * is populated.
 *
 * Thread-safety: instances are immutable after construction and safe to
 * share across threads.
 */
data class Theme(
    // Identity
    val schemaVersion: Int = 1,
    val id:            String,
    val name:          String,
    val author:        String? = null,
    val version:       String,
    val license:       String? = null,
    val homepage:      String? = null,
    val description:   String? = null,
    val minAppVersion: String? = null,
    val extends:       String? = null,

    // Mode
    val mode: Mode = Mode.Dark,

    // Token groups
    val color:      ColorTokens,
    val typography: TypographyTokens = TypographyTokens(),
    val shape:      ShapeTokens? = null,
    val spacing:    SpacingTokens? = null,
    val elevation:  List<ElevationLevel>? = null,
    val motion:     MotionTokens? = null,
    val opacity:    OpacityTokens? = null,
    val icons:      IconTokens? = null,
    val assets:     AssetTokens? = null,
    val a11y:       AccessibilityTokens? = null,
) {
    init {
        require(id.isNotBlank()) { "Theme id must not be blank" }
        require(id.matches(Regex("^[a-z0-9]([a-z0-9-]{1,62}[a-z0-9])?$"))) {
            "Theme id must match pattern ^[a-z0-9]([a-z0-9-]{1,62}[a-z0-9])?$"
        }
        require(version.matches(Regex("^\\d+\\.\\d+\\.\\d+$"))) {
            "Theme version must be X.Y.Z format"
        }
    }
}
