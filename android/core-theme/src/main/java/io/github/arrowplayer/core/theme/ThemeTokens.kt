// SPDX-License-Identifier: MPL-2.0
//
// ThemeTokens.kt — Canonical design tokens for Android.
//
// Spec: eclipse-player.md §12.1 (REQ-UIX-001), §11.2 (REQ-THM-010).
//
// The canonical token values live in shared-spec/design-system/tokens.json.
// Android consumes them via a build-time code generator; this module is the
// output of that generator and is the single source of truth for the Android
// app.  Both platforms must agree on every value; shared-spec/design-system/
// tokens.json is the canonical source of that agreement.
//
// All tokens are immutable after loading.  The ThemeTokens object is a Hilt
// singleton injectable via `@Inject constructor()`.
//
// Design token categories:
//   - Spacing: 4 px base unit, 8 named steps (xs through 4xl)
//   - Typography: 7 type scale steps (display, headline, title, body, label, caption, mono)
//   - Radius: 4 named steps (sm, md, lg, xl) + full
//   - Elevation: 5 ascending shadow levels
//   - Motion: 4 duration steps + 4 easing curves

package io.github.arrowplayer.core.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Canonical design token values.  Loaded from shared-spec/design-system/tokens.json
 * at build time by the code generator; the values here are the output of that
 * generator and must never be changed manually.
 *
 * All values are canonical (REQ-UIX-001): both platforms consume the same numbers.
 * A duplicate number is a number that will drift.
 *
 * @constructor Values are initialised from the generated constants below.
 *             Do not modify the values here; modify the source tokens.json instead.
 *
 * Thread-safety: instances are immutable and safe to share across threads.
 * Hilt scope: [Singleton].
 */
@Singleton
class ThemeTokens @Inject constructor() {

    // -------------------------------------------------------------------------
    //  Spacing tokens  (4 px base unit)
    // -------------------------------------------------------------------------

    val spacingXs: Dp = 4.dp
    val spacingSm: Dp = 8.dp
    val spacingMd: Dp = 12.dp
    val spacingLg: Dp = 16.dp
    val spacingXl: Dp = 24.dp
    val spacing2xl: Dp = 32.dp
    val spacing3xl: Dp = 48.dp
    val spacing4xl: Dp = 64.dp

    /** Base spacing unit in dp. */
    val spacingUnit: Dp = 4.dp

    /**
     * Resolve a spacing token by name.
     *
     * @param name The token name (e.g. "xs", "md", "xl").
     *              Case-insensitive.
     * @return The spacing value in dp, or [spacingMd] if unknown.
     */
    fun spacing(name: String): Dp = when (name.lowercase()) {
        "xs"  -> spacingXs
        "sm"  -> spacingSm
        "md"  -> spacingMd
        "lg"  -> spacingLg
        "xl"  -> spacingXl
        "2xl" -> spacing2xl
        "3xl" -> spacing3xl
        "4xl" -> spacing4xl
        else  -> spacingMd
    }

    // -------------------------------------------------------------------------
    //  Type scale tokens  (14 px base, 1.2 ratio)
    // -------------------------------------------------------------------------

    /** Display: 34 px / 1.15 / 700 / −0.5 */
    val typeDisplay: TextStyle = TextStyle(
        fontSize = 34.sp,
        lineHeight = (34 * 1.15f).sp,
        fontWeight = FontWeight.Bold,
        letterSpacing = (-0.5f).sp,
    )

    /** Headline: 24 px / 1.25 / 600 / −0.25 */
    val typeHeadline: TextStyle = TextStyle(
        fontSize = 24.sp,
        lineHeight = (24 * 1.25f).sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = (-0.25f).sp,
    )

    /** Title: 18 px / 1.3 / 600 / 0 */
    val typeTitle: TextStyle = TextStyle(
        fontSize = 18.sp,
        lineHeight = (18 * 1.3f).sp,
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 0.sp,
    )

    /** Body: 14 px / 1.5 / 400 / 0 */
    val typeBody: TextStyle = TextStyle(
        fontSize = 14.sp,
        lineHeight = (14 * 1.5f).sp,
        fontWeight = FontWeight.Normal,
        letterSpacing = 0.sp,
    )

    /** Label: 13 px / 1.4 / 500 / 0.1 */
    val typeLabel: TextStyle = TextStyle(
        fontSize = 13.sp,
        lineHeight = (13 * 1.4f).sp,
        fontWeight = FontWeight.Medium,
        letterSpacing = 0.1f.sp,
    )

    /** Caption: 12 px / 1.35 / 400 / 0.2 */
    val typeCaption: TextStyle = TextStyle(
        fontSize = 12.sp,
        lineHeight = (12 * 1.35f).sp,
        fontWeight = FontWeight.Normal,
        letterSpacing = 0.2f.sp,
    )

    /** Mono: 13 px / 1.45 / 400 / 0 */
    val typeMono: TextStyle = TextStyle(
        fontSize = 13.sp,
        lineHeight = (13 * 1.45f).sp,
        fontWeight = FontWeight.Normal,
        letterSpacing = 0.sp,
    )

    /** Base font size: 14 px. */
    val typeBaseSize: TextUnit = 14.sp

    /**
     * Resolve a type scale token by name.
     *
     * @param name Token name: "display", "headline", "title", "body",
     *             "label", "caption", "mono".
     * @return The [TextStyle], or [typeBody] if unknown.
     */
    fun typeStyle(name: String): TextStyle = when (name.lowercase()) {
        "display"  -> typeDisplay
        "headline" -> typeHeadline
        "title"    -> typeTitle
        "body"     -> typeBody
        "label"    -> typeLabel
        "caption"  -> typeCaption
        "mono"     -> typeMono
        else       -> typeBody
    }

    // -------------------------------------------------------------------------
    //  Radius tokens  (in dp)
    // -------------------------------------------------------------------------

    val radiusSm: Dp = 4.dp
    val radiusMd: Dp = 8.dp
    val radiusLg: Dp = 12.dp
    val radiusXl: Dp = 16.dp

    /** Full radius: pill shape (mapped to a very large value). */
    val radiusFull: Dp = Dp.Infinity

    /**
     * Resolve a radius token by name.
     *
     * @param name Token name: "none" (returns 0.dp), "sm", "md", "lg", "xl", "full".
     * @return The radius in dp.
     */
    fun radius(name: String): Dp = when (name.lowercase()) {
        "none"  -> 0.dp
        "sm"    -> radiusSm
        "md"    -> radiusMd
        "lg"    -> radiusLg
        "xl"    -> radiusXl
        "full"  -> radiusFull
        else    -> 0.dp
    }

    // -------------------------------------------------------------------------
    //  Elevation tokens  (offsetY / blur / alpha)
    // -------------------------------------------------------------------------

    /** Elevation level 1: offsetY=1, blur=2, alpha=0.10 */
    val elevation1: ElevationLevel = ElevationLevel(1.dp, 2.dp, 0.10f)

    /** Elevation level 2: offsetY=2, blur=4, alpha=0.12 */
    val elevation2: ElevationLevel = ElevationLevel(2.dp, 4.dp, 0.12f)

    /** Elevation level 3: offsetY=4, blur=8, alpha=0.14 */
    val elevation3: ElevationLevel = ElevationLevel(4.dp, 8.dp, 0.14f)

    /** Elevation level 4: offsetY=8, blur=16, alpha=0.16 */
    val elevation4: ElevationLevel = ElevationLevel(8.dp, 16.dp, 0.16f)

    /** Elevation level 5: offsetY=16, blur=32, alpha=0.20 */
    val elevation5: ElevationLevel = ElevationLevel(16.dp, 32.dp, 0.20f)

    /**
     * Resolve an elevation level by index (0-based).
     *
     * @param level Level index [0..4].
     * @return The [ElevationLevel], or [elevation1] if out of range.
     */
    fun elevation(level: Int): ElevationLevel = when (level) {
        0 -> elevation1
        1 -> elevation2
        2 -> elevation3
        3 -> elevation4
        4 -> elevation5
        else -> elevation1
    }

    // -------------------------------------------------------------------------
    //  Motion duration tokens  (in ms)
    // -------------------------------------------------------------------------

    /** Instant: 80 ms — state feedback on press. */
    val durationInstant: Int = 80

    /** Fast: 150 ms — hover, small transitions. */
    val durationFast: Int = 150

    /** Normal: 250 ms — panel transitions, skin cross-fade. */
    val durationNormal: Int = 250

    /** Slow: 400 ms — full-screen transitions. */
    val durationSlow: Int = 400

    /**
     * Resolve a duration token by name.
     *
     * @param name Token name: "instant", "fast", "normal", "slow".
     * @return Duration in ms, or [durationNormal] if unknown.
     */
    fun duration(name: String): Int = when (name.lowercase()) {
        "instant" -> durationInstant
        "fast"   -> durationFast
        "normal" -> durationNormal
        "slow"   -> durationSlow
        else     -> durationNormal
    }

    // -------------------------------------------------------------------------
    //  Motion easing tokens  (cubic-bezier control points)
    // -------------------------------------------------------------------------

    /** Standard easing: cubic-bezier(0.2, 0.0, 0.0, 1.0) */
    val easingStandard: CubicBezier = CubicBezier(0.2f, 0.0f, 0.0f, 1.0f)

    /** Decelerate easing: cubic-bezier(0.0, 0.0, 0.2, 1.0) */
    val easingDecelerate: CubicBezier = CubicBezier(0.0f, 0.0f, 0.2f, 1.0f)

    /** Accelerate easing: cubic-bezier(0.4, 0.0, 1.0, 1.0) */
    val easingAccelerate: CubicBezier = CubicBezier(0.4f, 0.0f, 1.0f, 1.0f)

    /** Emphasized easing: shares the standard curve, paired with slow duration. */
    val easingEmphasized: CubicBezier = easingStandard

    /**
     * Resolve an easing token by name.
     *
     * @param name Token name: "standard", "decelerate", "accelerate", "emphasized".
     * @return The [CubicBezier], or [easingStandard] if unknown.
     */
    fun easing(name: String): CubicBezier = when (name.lowercase()) {
        "standard"    -> easingStandard
        "decelerate" -> easingDecelerate
        "accelerate" -> easingAccelerate
        "emphasized"  -> easingEmphasized
        else          -> easingStandard
    }
}

// -------------------------------------------------------------------------
//  Supporting data classes
// -------------------------------------------------------------------------

/**
 * A shadow elevation level: offset, blur radius, and alpha.
 *
 * The shadow colour comes from the active theme, not from here —
 * a fixed shadow colour looks wrong in light mode.
 *
 * @param offsetY Vertical offset of the shadow in dp.
 * @param blur Blur radius of the shadow in dp.
 * @param alpha Base alpha of the shadow colour (0..1).
 */
data class ElevationLevel(
    val offsetY: Dp,
    val blur: Dp,
    val alpha: Float,
)

/**
 * A cubic-bezier easing curve, stored as four control points.
 *
 * Maps directly to `android.graphics.Path#cubicTo()`.
 *
 * @param x1 X coordinate of control point 1.
 * @param y1 Y coordinate of control point 1.
 * @param x2 X coordinate of control point 2.
 * @param y2 Y coordinate of control point 2.
 */
data class CubicBezier(
    val x1: Float,
    val y1: Float,
    val x2: Float,
    val y2: Float,
)
