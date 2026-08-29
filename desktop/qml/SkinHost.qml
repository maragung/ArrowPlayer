// SPDX-License-Identifier: MPL-2.0
//
// SkinHost.qml — QML skin surface host.
//
// Spec: eclipse-player.md §11 (REQ-THM-001 … REQ-THM-072), §6.1.
//
// SkinHost is a QML component that:
//   1. Reads a skin manifest from a .eclipseskin package.
//   2. Loads theme tokens into the QML context as var properties.
//   3. Instantiates the layout documents for each surface.
//   4. Renders Now Playing, Library, and Visualizer surfaces.
//   5. Supports hot-reload (REQ-THM-051) for developers.
//
// Theme tokens become QML `var` properties that QML bindings react to:
//   - color.* → Qt.color
//   - typography.scale.* → font descriptor
//   - spacing.* → integer px
//   - motion.duration.* → integer ms
//   - motion.easing.* → var { x1, y1, x2, y2 }
//
// All tokens are immutable after loading.  Re-applying a theme creates a new
// SkinHost instance and cross-fades (REQ-THM-050) rather than mutating
// existing properties in-place.

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Particles

// ---------------------------------------------------------------------------
//  SkinHost — the root of a skin-rendered surface
// ---------------------------------------------------------------------------

Item {
    id: root

    // ── Public API ────────────────────────────────────────────────────────

    /// The path to the active .eclipseskin package or theme.json.
    /// Setting this property loads and applies the skin.
    property string skinPath: ""

    /// True while a skin is being loaded.
    property bool loading: false

    /// The loaded skin's name, for display in the skin browser.
    property string skinName: ""

    /// Human-readable error message from the last failed load attempt.
    /// Empty when the last load succeeded.
    property string loadError: ""

    /// The loaded theme object (for direct token access).
    /// null when no theme is loaded.
    property var theme: null

    /// The loaded layout documents keyed by surface name.
    /// e.g. { "now-playing": LayoutDocument, "mini-player": LayoutDocument }
    property var layouts: ({})

    // ── Internal state ───────────────────────────────────────────────────

    // QML-side token bindings — these are the reactive surface that QML
    // bindings read.  When a new theme is applied we rebuild these from
    // the Theme object and QML property bindings propagate automatically.

    // Color tokens — exposed as Qt.color objects
    property var colorTokens: ({})

    // Typography tokens — exposed as font descriptors
    property var typographyTokens: ({})

    // Spacing tokens — exposed as integer px values
    property var spacingTokens: ({})

    // Motion tokens — exposed as { duration: ms, easing: { x1, y1, x2, y2 } }
    property var motionTokens: ({})

    // Radius tokens — exposed as integer px values
    property var radiusTokens: ({})

    // Elevation tokens — exposed as [{ offsetY, blur, alpha, color }, ...]
    property var elevationTokens: ([])

    // ── Methods ─────────────────────────────────────────────────────────

    /// Reload the current skin (used for hot-reload in developer mode).
    function reload() {
        if (skinPath) _loadSkin(skinPath)
    }

    /// Apply a theme object from the C++ side without loading from disk.
    /// Used by the skin installer and the theme override system.
    function applyTheme(themeObject) {
        _applyThemeToQml(themeObject)
    }

    // ── Private ──────────────────────────────────────────────────────────

    // Internal: actually load a skin from a path.
    function _loadSkin(path) {
        loading = true
        loadError = ""
        // The actual loading is done by the C++ SkinHostController
        // via a QML-invokable method.  This QML file is the rendering
        // surface; the C++ side manages the filesystem, ZIP extraction,
        // and manifest validation.
    }

    // Called from C++ once the theme is loaded and validated.
    // `themeObject` is a QVariant wrapping a shared_ptr<const Theme>.
    onThemeChanged: {
        if (!theme) return
        _applyThemeToQml(theme)
    }

    function _applyThemeToQml(themeObject) {
        // Expose color tokens to QML context as Qt.color values
        var colors = ({})

        function parseHexColor(hex) {
            // "#RRGGBB" or "#RRGGBBAA"
            if (!hex || hex.length < 7) return "black"
            var r = parseInt(hex.substring(1, 3), 16)
            var g = parseInt(hex.substring(3, 5), 16)
            var b = parseInt(hex.substring(5, 7), 16)
            var a = hex.length >= 9 ? parseInt(hex.substring(7, 9), 16) / 255 : 1.0
            return Qt.hsla(r / 255, g / 255, b / 255, a)
        }

        // Flatten the color object into a dotted-key map for easy binding.
        // e.g. "color.background.raised" → Qt.color
        function flattenColors(obj, prefix) {
            for (var key in obj) {
                var dotted = (prefix ? prefix + "." : "") + key
                if (typeof obj[key] === 'object' && obj[key] !== null) {
                    if ('hex' in obj[key]) {
                        // Terminal color node
                        colors[dotted] = parseHexColor(obj[key].hex)
                    } else {
                        flattenColors(obj[key], dotted)
                    }
                }
            }
        }

        if (themeObject.color) flattenColors(themeObject.color, "color")
        colorTokens = colors

        // Expose typography tokens as font descriptors
        var typeScale = {}
        function parseTypeScale(scale, prefix) {
            for (var key in scale) {
                if (typeof scale[key] === 'object' && scale[key] !== null) {
                    var dotted = (prefix ? prefix + "." : "") + key
                    if ('size' in scale[key]) {
                        // Terminal type style node
                        typeScale[dotted] = {
                            pixelSize: Math.round(scale[key].size || 14),
                            weight: (scale[key].weight || 400) / 10,  // Font weight enum
                            lineHeight: scale[key].lineHeight || 1.5,
                            letterSpacing: scale[key].letterSpacing || 0,
                        }
                    } else {
                        parseTypeScale(scale[key], dotted)
                    }
                }
            }
        }

        if (themeObject.typography && themeObject.typography.scale)
            parseTypeScale(themeObject.typography.scale, "typography.scale")
        typographyTokens = typeScale

        // Spacing tokens as integer px
        var spacing = {}
        if (themeObject.spacing) {
            var s = themeObject.spacing
            if (s.xs !== undefined) spacing.xs = s.xs
            if (s.sm !== undefined) spacing.sm = s.sm
            if (s.md !== undefined) spacing.md = s.md
            if (s.lg !== undefined) spacing.lg = s.lg
            if (s.xl !== undefined) spacing.xl = s.xl
            if (s['2xl'] !== undefined) spacing['2xl'] = s['2xl']
            if (s['3xl'] !== undefined) spacing['3xl'] = s['3xl']
            if (s['4xl'] !== undefined) spacing['4xl'] = s['4xl']
        }
        spacingTokens = spacing

        // Radius tokens as integer px
        var radius = {}
        if (themeObject.shape && themeObject.shape.radius) {
            var r = themeObject.shape.radius
            if (r.sm !== undefined) radius.sm = r.sm
            if (r.md !== undefined) radius.md = r.md
            if (r.lg !== undefined) radius.lg = r.lg
            if (r.xl !== undefined) radius.xl = r.xl
            radius.full = 9999
        }
        radiusTokens = radius

        // Motion tokens as { duration: ms, easing: { x1, y1, x2, y2 } }
        var motion = { duration: {}, easing: {} }
        if (themeObject.motion) {
            if (themeObject.motion.duration) {
                var d = themeObject.motion.duration
                if (d.instant !== undefined) motion.duration.instant = d.instant
                if (d.fast !== undefined) motion.duration.fast = d.fast
                if (d.normal !== undefined) motion.duration.normal = d.normal
                if (d.slow !== undefined) motion.duration.slow = d.slow
            }
            if (themeObject.motion.easing) {
                var e = themeObject.motion.easing
                for (var ek in e) {
                    if (e[ek] && e[ek].x1 !== undefined)
                        motion.easing[ek] = e[ek]
                }
            }
        }
        motionTokens = motion

        // Elevation as array of { offsetY, blur, alpha, color }
        var elevation = []
        if (themeObject.elevation) {
            for (var i = 0; i < themeObject.elevation.length; ++i) {
                var el = themeObject.elevation[i]
                elevation.push({
                    offsetY: el.offsetY || 0,
                    blur: el.blur || 0,
                    alpha: el.alpha !== undefined ? el.alpha : 0,
                    color: el.color ? parseHexColor(el.color.hex || el.color) : "transparent"
                })
            }
        }
        elevationTokens = elevation
    }

    // ── Layout instantiation ────────────────────────────────────────────

    // The layout tree is rendered by SkinSurface.qml, which consumes
    // the layout document and the token bindings above.

    // Per-surface layout instances — these are the actual rendered surfaces.
    // They are shown/hidden by the application shell based on navigation state.

    property var _surfaceInstances: ({})

    function _instantiateLayout(surfaceName, layoutDocument) {
        // SkinSurface is instantiated dynamically by the C++ layout engine.
        // This QML component provides the context; actual node instantiation
        // is done by the LayoutRenderer C++ class which produces Qt Object
        // trees that are parented to this Item.
        _surfaceInstances[surfaceName] = layoutDocument
    }

    // ── Hot-reload support ───────────────────────────────────────────────
    // In developer mode (--dev-skins), the C++ side watches the unpacked
    // skin directory and calls reload() within 300 ms of a change.
    // The cross-fade is handled by the application shell via a
    // SequentialAnimation on opacity.

    // ── Error display ───────────────────────────────────────────────────
    // Any load error is shown as an in-app overlay rather than a dialog.
    // This matches REQ-THM-051 and keeps the author in the app.

    Loader {
        id: errorOverlay
        anchors.fill: parent
        active: root.loadError !== ""
        sourceComponent: Rectangle {
            color: "#aa000000"
            Column {
                anchors.centerIn: parent
                spacing: 8
                Text {
                    text: "Skin Error"
                    color: "#ff6060"
                    font.pixelSize: 16
                    font.bold: true
                }
                Text {
                    text: root.loadError
                    color: "#ffffff"
                    font.pixelSize: 13
                    width: root.width - 48
                    wrapMode: Text.Wrap
                }
                Button {
                    text: "Dismiss"
                    onClicked: root.loadError = ""
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  SkinSurface — renders a single layout document
//
//  This component takes a layout node tree (produced by the C++ LayoutRenderer)
//  and renders it as a QML object tree.  The C++ side does the parsing and
//  budget enforcement; this QML component does the rendering.
//
//  Each node type maps to a QML type:
//    Container types → Item with a RowLayout/ColumnLayout/GridLayout
//    Text / Marquee   → Text with a MarqueeBehavior attached
//    Button / ToggleButton → Button / CheckBox
//    Slider / SeekBar / VolumeControl → Slider
//    TransportBar → Row of Buttons
//    Visualizer  → VisualizerRenderer (C++ QML plugin)
//    AlbumArt    → Image with fallback
//    ... etc.
// ---------------------------------------------------------------------------

Item {
    id: surface

    required property var layoutRoot  // the root Node from the layout document
    property var bindings: ({})       // token bindings: { "color.text.primary": Qt.color, ... }

    // Resolve a color token reference like "color.background.raised"
    function resolveColor(ref) {
        if (!ref || typeof ref !== 'string') return "black"
        // Strip leading "color." prefix if present
        var key = ref.startsWith("color.") ? ref : ("color." + ref)
        return bindings[key] || "black"
    }

    // Resolve a spacing token like "md" or "lg"
    function resolveSpacing(name) {
        if (!name || typeof name !== 'string') return 0
        // Named spacing tokens are passed as integers from the C++ layer
        if (typeof name === 'number') return name
        // String token names: look up in the theme's spacing object
        return 0  // Fallback; the C++ layer resolves these
    }

    // Build the QML component tree from the layout node tree.
    // This is called once on load; the result is cached.
    function buildTree() {
        // The actual tree building is done by the C++ LayoutRenderer,
        // which produces QML-compatible QObject trees.  This QML component
        // serves as the parent Item for those trees.
        // The C++ code calls QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership)
        // and then Reparent the created objects to 'surface'.
    }
}

// ---------------------------------------------------------------------------
//  TokenBinding — attached property for declarative token references
//
//  Usage in QML:
//    Rectangle {
//        color: TokenBinding.get("color.background.base")
//        border.color: TokenBinding.get("color.border.base")
//    }
// ---------------------------------------------------------------------------

// TokenBinding is used by QML components to bind to theme tokens reactively.
// When a new theme is applied, the token map is updated and QML property
// bindings automatically re-resolve.
QtObject {
    id: tokenBindingHelper

    // Singleton token map — populated by SkinHost
    property var tokenMap: ({})

    function get(path, defaultValue) {
        if (tokenMap && tokenMap[path] !== undefined)
            return tokenMap[path]
        return defaultValue !== undefined ? defaultValue : "transparent"
    }
}

// Export tokenBindingHelper as an attached property context singleton
// so QML files can write: color: TokenBinding.get("color.text.primary")
// (In practice this is done via SkinHost.colorTokens in context properties)
