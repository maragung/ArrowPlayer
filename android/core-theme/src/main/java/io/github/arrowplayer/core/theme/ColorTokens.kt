// SPDX-License-Identifier: MPL-2.0
//
// ColorTokens.kt — Colour token definitions and utilities.
//
// Spec: eclipse-player.md §11.2 (REQ-THM-010), §12.1 (REQ-UIX-001).
//
// Colour tokens are defined per-theme (e.g. Eclipse Dark, Eclipse Light).
// Each theme provides values for the required roles; optional roles fall back
// to the built-in theme.  This file defines the colour token hierarchy.
//
// The token hierarchy follows the schema:
//   color.background  (base, sunken, raised, overlay, scrim)
//   color.surface    (base, hover, pressed, selected, disabled)
//   color.text       (primary, secondary, tertiary, disabled, inverse, onAccent, link)
//   color.accent     (base, hover, pressed, subtle, muted)
//   color.border     (base, subtle, strong, focus)
//   color.state      (success, warning, error, info)
//   color.playback   (progress, progressTrack, buffered, waveform,
//                     waveformPlayed, peakMeter, peakMeterClip)
//   color.visualizer (palette: Color[], background)
//
// All colours are 8-digit hex strings "#RRGGBBAA".  The Color type alias
// is androidx.compose.ui.graphics.Color.

package io.github.arrowplayer.core.theme

import androidx.compose.ui.graphics.Color

// ---------------------------------------------------------------------------
//  Colour name aliases  (for readability in theme files)
// ---------------------------------------------------------------------------

/** An RGBA colour as an 8-digit hex string "#RRGGBBAA". */
typealias HexColor = String

/** Parse a "#RRGGBB" or "#RRGGBBAA" string into a Compose [Color]. */
fun HexColor.toComposeColor(): Color {
    val hex = this.removePrefix("#")
    return when (hex.length) {
        6 -> Color(android.graphics.Color.parseColor("#$hex"))
        8 -> Color(android.graphics.Color.parseColor("#$hex"))
        else -> Color.Black
    }
}

// ---------------------------------------------------------------------------
//  Background colours
// ---------------------------------------------------------------------------

/**
 * Background colour tokens: the canvas on which everything else sits.
 *
 * @param base     Required. The primary background.
 * @param sunken   Deeper than base, for inset areas (e.g. input fields).
 * @param raised   Lighter than base, for surfaces elevated above base.
 * @param overlay  Semi-transparent overlay for modals, drawers, etc.
 * @param scrim    Full-screen scrim behind dialogs.
 */
data class BackgroundTokens(
    val base:     HexColor,
    val sunken:   HexColor? = null,
    val raised:   HexColor? = null,
    val overlay:  HexColor? = null,
    val scrim:    HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Surface colours
// ---------------------------------------------------------------------------

/**
 * Surface colour tokens: interactive elements and content containers.
 *
 * @param base      Required. The default surface colour.
 * @param hover     Colour when hovered.
 * @param pressed   Colour when pressed.
 * @param selected  Colour when selected.
 * @param disabled  Colour when disabled.
 */
data class SurfaceTokens(
    val base:     HexColor,
    val hover:    HexColor? = null,
    val pressed:  HexColor? = null,
    val selected: HexColor? = null,
    val disabled: HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Text colours
// ---------------------------------------------------------------------------

/**
 * Text colour tokens: all text roles.
 *
 * @param primary    Required. Default text.
 * @param secondary  Required. Secondary / supporting text.
 * @param tertiary   Supporting text below secondary.
 * @param disabled   Text on disabled elements.
 * @param inverse    Text on coloured backgrounds (inverted from primary).
 * @param onAccent   Text on accent colour.
 * @param link       Hyperlink text.
 */
data class TextTokens(
    val primary:   HexColor,
    val secondary: HexColor,
    val tertiary:  HexColor? = null,
    val disabled:  HexColor? = null,
    val inverse:   HexColor? = null,
    val onAccent:  HexColor? = null,
    val link:      HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Accent colours
// ---------------------------------------------------------------------------

/**
 * Accent colour tokens: the primary brand colour and its variants.
 *
 * @param base    Required. The primary accent colour.
 * @param hover   Accent on hover.
 * @param pressed Accent on press.
 * @param subtle  Low-contrast accent for backgrounds.
 * @param muted   Desaturated accent for borders and dividers.
 */
data class AccentTokens(
    val base:    HexColor,
    val hover:   HexColor? = null,
    val pressed: HexColor? = null,
    val subtle:  HexColor? = null,
    val muted:   HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Border colours
// ---------------------------------------------------------------------------

/**
 * Border colour tokens.
 *
 * @param base    Required. Default border colour.
 * @param subtle  Low-contrast border.
 * @param strong  High-contrast border.
 * @param focus   Focus ring colour (a11y.focusRingWidth wide).
 */
data class BorderTokens(
    val base:   HexColor,
    val subtle: HexColor? = null,
    val strong: HexColor? = null,
    val focus:  HexColor? = null,
)

// ---------------------------------------------------------------------------
//  State colours
// ---------------------------------------------------------------------------

/**
 * Semantic state colour tokens.
 *
 * @param success Operation succeeded.
 * @param warning User attention needed, not an error.
 * @param error   Operation failed.
 * @param info    Informational message.
 */
data class StateTokens(
    val success: HexColor? = null,
    val warning: HexColor? = null,
    val error:   HexColor? = null,
    val info:    HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Playback colours
// ---------------------------------------------------------------------------

/**
 * Playback-specific colour tokens for progress bars, waveforms, and meters.
 *
 * @param progress        Progress / seek position colour.
 * @param progressTrack  Track behind the progress indicator.
 * @param buffered       Buffered-but-not-played region.
 * @param waveform       Unplayed waveform colour.
 * @param waveformPlayed Played waveform colour.
 * @param peakMeter      Peak meter body colour.
 * @param peakMeterClip  Peak meter clip indicator.
 */
data class PlaybackTokens(
    val progress:        HexColor? = null,
    val progressTrack:   HexColor? = null,
    val buffered:         HexColor? = null,
    val waveform:         HexColor? = null,
    val waveformPlayed:   HexColor? = null,
    val peakMeter:       HexColor? = null,
    val peakMeterClip:   HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Visualizer colours
// ---------------------------------------------------------------------------

/**
 * Visualizer colour tokens.
 *
 * @param palette   Array of colours for the spectrum palette (1–16 colours).
 *                  [REQ-UIX-037] uses these for the native spectrum bars.
 * @param background Background behind the visualizer.
 */
data class VisualizerTokens(
    val palette:     List<HexColor>? = null,
    val background:  HexColor? = null,
)

// ---------------------------------------------------------------------------
//  Full colour token bundle
// ---------------------------------------------------------------------------

/**
 * Complete colour token set for a theme.
 *
 * This is the C++ [ColorTokens] equivalent in Kotlin.  Both are
 * initialised from the same theme.json by the skin loader.
 */
data class ColorTokens(
    val background:  BackgroundTokens,
    val surface:    SurfaceTokens,
    val text:       TextTokens,
    val accent:     AccentTokens,
    val border:     BorderTokens,
    val state:      StateTokens = StateTokens(),
    val playback:   PlaybackTokens = PlaybackTokens(),
    val visualizer: VisualizerTokens = VisualizerTokens(),
)

/**
 * Resolve a dotted colour token path to a Compose [Color].
 *
 * Example: `resolve("color.text.primary")` → the primary text colour.
 *
 * @param path A dotted path into the colour token tree,
 *             e.g. "color.background.raised" or "color.accent.base".
 * @return The resolved colour, or [Color.Black] if the path is not found.
 */
fun ColorTokens.resolve(path: String): Color {
    val segments = path.removePrefix("color.").split(".")
    return when {
        segments.size < 2 -> Color.Black
        else -> when (segments[0]) {
            "background"  -> resolveBackground(segments.drop(1))
            "surface"    -> resolveSurface(segments.drop(1))
            "text"       -> resolveText(segments.drop(1))
            "accent"     -> resolveAccent(segments.drop(1))
            "border"     -> resolveBorder(segments.drop(1))
            "state"      -> resolveState(segments.drop(1))
            "playback"   -> resolvePlayback(segments.drop(1))
            "visualizer" -> resolveVisualizer(segments.drop(1))
            else         -> Color.Black
        }
    }
}

private fun ColorTokens.resolveBackground(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "base"     -> background.base.toComposeColor()
        "sunken"   -> background.sunken?.toComposeColor()   ?: background.base.toComposeColor()
        "raised"   -> background.raised?.toComposeColor()   ?: background.base.toComposeColor()
        "overlay"  -> background.overlay?.toComposeColor() ?: background.base.toComposeColor()
        "scrim"    -> background.scrim?.toComposeColor()    ?: background.base.toComposeColor()
        else       -> Color.Black
    }
}

private fun ColorTokens.resolveSurface(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "base"     -> surface.base.toComposeColor()
        "hover"    -> surface.hover?.toComposeColor()    ?: surface.base.toComposeColor()
        "pressed"  -> surface.pressed?.toComposeColor() ?: surface.base.toComposeColor()
        "selected" -> surface.selected?.toComposeColor() ?: surface.base.toComposeColor()
        "disabled" -> surface.disabled?.toComposeColor() ?: surface.base.toComposeColor()
        else       -> Color.Black
    }
}

private fun ColorTokens.resolveText(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "primary"   -> text.primary.toComposeColor()
        "secondary" -> text.secondary.toComposeColor()
        "tertiary"  -> text.tertiary?.toComposeColor()  ?: text.secondary.toComposeColor()
        "disabled"  -> text.disabled?.toComposeColor()  ?: text.secondary.toComposeColor()
        "inverse"   -> text.inverse?.toComposeColor()   ?: text.primary.toComposeColor()
        "onAccent"  -> text.onAccent?.toComposeColor()  ?: text.primary.toComposeColor()
        "link"      -> text.link?.toComposeColor()      ?: text.primary.toComposeColor()
        else        -> Color.Black
    }
}

private fun ColorTokens.resolveAccent(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "base"    -> accent.base.toComposeColor()
        "hover"   -> accent.hover?.toComposeColor()   ?: accent.base.toComposeColor()
        "pressed" -> accent.pressed?.toComposeColor() ?: accent.base.toComposeColor()
        "subtle"  -> accent.subtle?.toComposeColor()  ?: accent.base.toComposeColor()
        "muted"   -> accent.muted?.toComposeColor()   ?: accent.base.toComposeColor()
        else      -> Color.Black
    }
}

private fun ColorTokens.resolveBorder(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "base"   -> border.base.toComposeColor()
        "subtle" -> border.subtle?.toComposeColor() ?: border.base.toComposeColor()
        "strong" -> border.strong?.toComposeColor() ?: border.base.toComposeColor()
        "focus"  -> border.focus?.toComposeColor()  ?: border.base.toComposeColor()
        else     -> Color.Black
    }
}

private fun ColorTokens.resolveState(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "success" -> state.success?.toComposeColor() ?: Color(0xFF4CAF50)
        "warning" -> state.warning?.toComposeColor() ?: Color(0xFFFFC107)
        "error"   -> state.error?.toComposeColor()   ?: Color(0xFFF44336)
        "info"    -> state.info?.toComposeColor()    ?: Color(0xFF2196F3)
        else      -> Color.Black
    }
}

private fun ColorTokens.resolvePlayback(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "progress"        -> playback.progress?.toComposeColor()        ?: accent.base.toComposeColor()
        "progressTrack"   -> playback.progressTrack?.toComposeColor()   ?: surface.base.toComposeColor()
        "buffered"        -> playback.buffered?.toComposeColor()        ?: surface.base.toComposeColor()
        "waveform"       -> playback.waveform?.toComposeColor()        ?: text.secondary.toComposeColor()
        "waveformPlayed" -> playback.waveformPlayed?.toComposeColor()  ?: accent.base.toComposeColor()
        "peakMeter"      -> playback.peakMeter?.toComposeColor()       ?: accent.base.toComposeColor()
        "peakMeterClip"  -> playback.peakMeterClip?.toComposeColor()   ?: Color.Red
        else             -> Color.Black
    }
}

private fun ColorTokens.resolveVisualizer(path: List<String>): Color {
    if (path.isEmpty()) return Color.Black
    return when (path[0]) {
        "background" -> visualizer.background?.toComposeColor() ?: background.base.toComposeColor()
        "palette"    -> Color.Black  // palette is a list, not a single colour
        else         -> Color.Black
    }
}
