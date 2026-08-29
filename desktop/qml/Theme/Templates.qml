// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
//
// Theme tokens singleton — spec §7.1 layer 5 (PRESENTATION), §12.1.
//
// Provides ThemeTokens-style properties sourced from the canonical tokens.json
// (shared-spec/design-system/tokens.json).  Consumed by all QML surfaces as
// `Theme.spacing.lg`, `Theme.colors.primary`, etc.
//
// This is a QML singleton so all QML files can `import "qrc:/qml/Theme"` and
// get the same token values without each file needing to instantiate anything.

pragma Singleton
import QtQuick

QtObject {
    // ── Spacing ─────────────────────────────────────────────────────────────
    readonly property int spacing_xs:   4
    readonly property int spacing_sm:   8
    readonly property int spacing_md:   12
    readonly property int spacing_lg:   16
    readonly property int spacing_xl:   24
    readonly property int spacing_2xl:  32
    readonly property int spacing_3xl:  48
    readonly property int spacing_4xl:  64

    // ── Border radius ───────────────────────────────────────────────────────
    readonly property int radius_sm:   4
    readonly property int radius_md:   8
    readonly property int radius_lg:   12
    readonly property int radius_xl:   16
    readonly property int radius_full: 9999   // pill / full-round

    // ── Motion durations (ms) ───────────────────────────────────────────────
    readonly property int motion_instant:   80
    readonly property int motion_fast:     150
    readonly property int motion_normal:   250
    readonly property int motion_slow:     400

    // ── Motion duration helper ──────────────────────────────────────────────
    function duration(name) {
        switch (name) {
            case "instant": return 80;
            case "fast":   return 150;
            case "normal":  return 250;
            case "slow":   return 400;
            default:       return 250;
        }
    }

    // ── Colors — nested QtObject so QML accesses them as Theme.colors.primary ──
    // These are the light-theme defaults.  The C++ shell can inject resolved
    // values via QQmlContext; these fallbacks keep surfaces readable at all times.
    readonly property QtObject colors: QtObject {
        // Primary
        readonly property color primary:            "#5c3d99"
        readonly property color onPrimary:          "#ffffff"
        readonly property color primaryContainer:   "#eaddff"
        readonly property color onPrimaryContainer: "#21005d"

        // Secondary
        readonly property color secondary:           "#625b71"
        readonly property color onSecondary:         "#ffffff"
        readonly property color secondaryContainer:  "#e8def8"
        readonly property color onSecondaryContainer: "#1d192b"

        // Tertiary
        readonly property color tertiary:            "#7d5260"
        readonly property color onTertiary:         "#ffffff"
        readonly property color tertiaryContainer:   "#ffd8e4"
        readonly property color onTertiaryContainer: "#31111d"

        // Surface / background
        readonly property color background:           "#ffFBFE"
        readonly property color onBackground:         "#1C1B1F"
        readonly property color surface:              "#ffFBFE"
        readonly property color onSurface:           "#1C1B1F"
        readonly property color surfaceVariant:       "#E7E0EC"
        readonly property color surfaceContainerHighest: "#E6E0E9"
        readonly property color surfaceContainerHigh: "#E6E0E9"
        readonly property color onSurfaceVariant:    "#49454F"
        readonly property color onSurfaceSecondary:  "#625B71"
        readonly property color onSurfaceTertiary:   "#7D5260"
        readonly property color onSurfaceDisabled:    "#49454F"

        // Outline
        readonly property color outline:             "#79747E"
        readonly property color outlineVariant:      "#CAC4D0"

        // Error
        readonly property color error:              "#B3261E"
        readonly property color onError:           "#ffffff"
        readonly property color errorContainer:      "#F9DEDC"
        readonly property color onErrorContainer:    "#410E0B"
    }
}
