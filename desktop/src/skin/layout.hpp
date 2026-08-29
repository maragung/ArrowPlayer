// SPDX-License-Identifier: MPL-2.0
//
// layout.hpp — Declarative layout document interpreter.
//
// Spec: eclipse-player.md §11.4 (REQ-THM-025 … REQ-THM-034), §11.5 (REQ-THM-040).
//
// A layout document (`.eclayout`) is a JSON file validated against
// shared-spec/schemas/layout.schema.json.  The interpreter parses it into a
// tree of strongly-typed node structs.  The resulting tree is consumed by the
// QML skin host (SkinHost.qml), which instantiates Qt Quick controls for
// each node type.
//
// Design constraints imposed by the spec:
//   - No code execution: the interpreter is a pure read-only traversal.
//   - Closed vocabulary: only the 30 node types in componentType enum are
//     permitted; unknown types are rejected at schema-validation time.
//   - Bindings are read-only paths into a whitelist; actions are enum-only.
//   - EFS evaluation is deferred to the QML layer (the EFS engine exists
//     separately and is not duplicated here).
//   - The `when:` predicate is parsed here into an AST but evaluated at
//     runtime by the QML bindings.
//
// Layout document limits (enforced at parse time):
//   - Maximum 500 component instances (REQ-THM-033).
//   - Maximum nesting depth 24 (REQ-THM-033).
//   - Maximum 64 bindings per component tree (REQ-THM-033).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "theme/schema.hpp"

namespace arrow::skin {

// ---------------------------------------------------------------------------
//  Node types
//
//  Each layout node is a tagged struct carrying the properties that are
//  valid for that node type.  Properties not applicable to a type are
//  absent (std::nullopt).  The set of types matches the componentType enum
//  in layout.schema.json (REQ-THM-026).
// ---------------------------------------------------------------------------

// Forward declarations
struct Node;
struct ContainerNode;

// ---------------------------------------------------------------------------
//  Layout primitives
// ---------------------------------------------------------------------------

/// A dimension: either a fixed pixel value, "fill" (take available space),
/// "auto" (size-to-content), or a percentage of the parent.
struct Dimension {
    enum class Kind { px, fill, auto_, percent } kind{Kind::px};
    double value{0.0};  // valid when kind==px or kind==percent

    static Dimension px(double v)    { Dimension d; d.kind = Kind::px;      d.value = v; return d; }
    static Dimension fill()            { Dimension d; d.kind = Kind::fill;    return d; }
    static Dimension auto_()           { Dimension d; d.kind = Kind::auto_;   return d; }
    static Dimension percent(double v){ Dimension d; d.kind = Kind::percent; d.value = v; return d; }
};

/// A 2-D size constraint.
struct Size {
    std::optional<Dimension> width;
    std::optional<Dimension> height;
};

/// A 2-D sizing constraint with min/max bounds.
struct Sizing {
    std::optional<Dimension> width;
    std::optional<Dimension> height;
    std::optional<Dimension> min_width;
    std::optional<Dimension> min_height;
    std::optional<Dimension> max_width;
    std::optional<Dimension> max_height;
    std::optional<double>    grow;   // flex grow factor
};

/// A colour reference as a dotted path into the theme's colour object.
/// e.g. "color.background.raised".  The interpreter resolves this to a
/// theme token name; the QML layer resolves the name to a QColor.
struct ColorRef {
    std::string path;  // e.g. "color.text.primary"

    /// Return the resolved token name for logging/debugging.
    std::string_view token_name() const noexcept;
};

/// A spacing value: either a §12.1 token name or a raw pixel value.
struct SpacingValue {
    std::variant<std::string_view, std::int32_t> value;  // token name or px

    bool is_token()   const noexcept { return std::holds_alternative<std::string_view>(value); }
    bool is_pixels() const noexcept { return std::holds_alternative<std::int32_t>(value); }
};

/// A border descriptor.
struct Border {
    ColorRef          color;
    std::double_t    width{1.0};
};

/// A radius value: a §12.1 token name, "none", or a raw pixel value.
struct RadiusValue {
    std::variant<std::string_view, std::int32_t> value;  // token name or px

    bool is_none()   const noexcept;
    bool is_token()  const noexcept { return std::holds_alternative<std::string_view>(value); }
    bool is_pixels() const noexcept { return std::holds_alternative<std::int32_t>(value); }
};

/// Text overflow behaviour.
enum class Overflow { clip, ellipsis, wrap, marquee };

/// Text alignment.
enum class TextAlign { start, center, end };

/// Orientation.
enum class Orientation { horizontal, vertical };

/// Image fit mode.
enum class ImageFit { cover, contain, fill, none };

/// Album art fallback strategy.
enum class FallbackMode { placeholder, none_, blurred_color };

/// Visualizer renderer name.
enum class VisualizerStyle { bars, oscilloscope, spectrum, vu_meter, none_ };

/// Scroll direction.
enum class ScrollDirection { vertical, horizontal, both };

/// Alignment within a container.
enum class Align { start, center, end, stretch, baseline };

/// Justify within a container.
enum class Justify { start, center, end, space_between, space_around };

/// A state-path binding.  §11.4 / REQ-THM-027: read-only paths into a
/// whitelisted presentation state model.  The whitelist is exhaustive.
struct StatePath {
    std::string path;  // e.g. "track.title"
};

/// An EFS pattern string.  §10 / REQ-EFS-001: deferred to the QML layer.
struct EfsPattern {
    std::string pattern;  // e.g. "%artist% - %title%"
};

/// A command action.  §11.4 / REQ-THM-028: enum-only, mirrors §13.2.
struct Action {
    std::string id;  // e.g. "player.playPause"
};

/// A column definition for a ListView.
struct ListColumn {
    EfsPattern    efs;
    std::optional<std::string>         header;
    std::optional<Dimension>           width;
    std::optional<Align>               align;
    std::optional<std::string>         style;  // typography.scale.xxx
};

/// A tab definition for a TabBar.
struct Tab {
    std::string       label;
    std::string       icon;   // icon name in the icon set
    Action            action;
};

/// Transport bar button enum — a subset of the §13.2 command registry.
enum class TransportButton {
    previous, play_pause, play, pause, stop, next,
    shuffle, repeat, stop_after_current, loved, rating,
};

/// ---------------------------------------------------------------------------
///  when: predicate AST
///
///  REQ-THM-030: a restricted boolean predicate for conditional visibility.
///  Grammar: when = clause { ("and"|"or") clause };
///           clause = [ "not" ] atom;
///           atom   = state-path [operator literal];
///
///  The interpreter parses this into a flat AST; the QML layer evaluates it
///  against the live state model.
// ---------------------------------------------------------------------------

struct WhenPredicate {
    enum class Op { eq, ne, gt, lt, gte, lte } op;
    std::string path;   // state path
    std::string value; // string form; comparison is done by the QML layer

    static WhenPredicate eq(std::string p, std::string v) { return {Op::eq, std::move(p), std::move(v)}; }
};

struct WhenClause {
    bool                   negated{false};
    std::string            path;  // state path (for bare path checks)
    std::optional<WhenPredicate> pred;  // set if operator+value present
};

struct WhenExpression {
    enum class Connective { and_, or_ } connective;
    std::vector<WhenClause> clauses;
};

// ---------------------------------------------------------------------------
//  Node types
// ---------------------------------------------------------------------------

/// Layout node type tag.  Matches layout.schema.json componentType enum.
enum class NodeType : std::uint8_t {
    // Containers
    Stack, Row, Column, Grid, Panel, Spacer, ScrollArea, SplitPane,
    // Text
    Text, Marquee,
    // Image / Icon
    Icon, Image, AlbumArt,
    // Decorations
    Divider, Badge,
    // Interactive
    Button, ToggleButton, Slider, VolumeControl, TransportBar, SearchField, TabBar,
    // Lists
    ListView,
    // Media
    Visualizer, SeekBar, PeakMeter, WaveformView, LyricsView,
    // Rating / Progress
    Rating, ProgressBar,
};

/// Return the human-readable name for a NodeType.
constexpr std::string_view to_string(NodeType t) noexcept;

/// True if this node type may have child nodes.
constexpr bool has_children(NodeType t) noexcept;

// ---------------------------------------------------------------------------
//  Node — the base of every layout node
// ---------------------------------------------------------------------------

struct Node {
    // Identity
    NodeType                  type{NodeType::Panel};
    std::string               id;          // optional author-facing name
    std::optional<WhenExpression> when;    // visibility predicate

    // Layout
    std::optional<SpacingValue>  padding;
    std::optional<SpacingValue>  margin;
    std::optional<SpacingValue>  spacing;
    std::optional<Sizing>       sizing;
    std::optional<Size>         size;
    std::optional<Dimension>     width;
    std::optional<Dimension>     height;
    std::optional<Align>         align;
    std::optional<Justify>       justify;

    // Appearance
    std::optional<ColorRef>      background;
    std::optional<Border>        border;
    std::optional<RadiusValue>   radius;
    std::optional<std::int32_t> elevation;  // 0..5
    std::optional<double>        opacity;   // 0..1
    std::optional<bool>          clip;

    // Children (for container types)
    std::vector<Node>           children;

    // Text content (Text, Marquee)
    std::optional<std::string>         text;
    std::optional<EfsPattern>          efs;
    std::optional<StatePath>           bind;
    std::optional<std::string>         style;       // typography.scale.xxx or visualizer style
    std::optional<ColorRef>            color;
    std::optional<Overflow>            overflow;
    std::optional<std::int32_t>        max_lines;
    std::optional<TextAlign>           text_align;
    std::optional<std::string>         tooltip;
    std::optional<std::string>         accessible_name;
    std::optional<WhenExpression>      enabled_when;

    // Actions (Button, ToggleButton)
    std::optional<Action>               action;

    // Image / Icon
    std::optional<std::string>          source;   // assetRef for Image
    std::optional<std::string>         icon;     // icon name for Icon
    std::optional<double>              icon_size;
    std::optional<ImageFit>            fit;
    std::optional<FallbackMode>        fallback;

    // Grid
    std::optional<std::int32_t>        columns;
    std::optional<std::int32_t>        rows;
    std::optional<Orientation>          orientation;

    // ScrollArea / ListView
    std::optional<ScrollDirection>      scroll_direction;

    // SplitPane
    std::optional<double>              split;   // 0..1 split ratio

    // Slider / SeekBar / VolumeControl / SeekBar / ProgressBar
    std::optional<double>              min;
    std::optional<double>              max;
    std::optional<double>              step;
    std::optional<bool>               show_buffered;
    std::optional<bool>               show_labels;
    std::optional<std::string>        placeholder;
    std::optional<std::int32_t>       bar_count;

    // TransportBar
    std::vector<TransportButton>      buttons;

    // SearchField
    // (placeholder is used)

    // ListView
    std::vector<ListColumn>           columns_spec;

    // TabBar
    std::vector<Tab>                 tabs;

    // Rating
    std::optional<std::int32_t>      max_stars;

    // Marquee
    std::optional<double>             speed;   // px/s

    // LyricsView / WaveformView / Visualizer / PeakMeter
    // (style is used)

    // Derived: count of bindings in this subtree (for budget enforcement)
    std::int32_t                     binding_count{0};
};

// ---------------------------------------------------------------------------
//  LayoutDocument — the parsed layout file
// ---------------------------------------------------------------------------

struct LayoutDocument {
    std::int32_t                     schema_version{1};
    std::string                       surface;   // "main-window" | "now-playing" | ...
    std::optional<Size>               min_size;
    std::optional<std::string>        description;
    Node                              root;

    /// The total number of nodes in this document.
    std::int32_t count_nodes() const noexcept;

    /// The nesting depth of the deepest node.
    std::int32_t max_depth() const noexcept;

    /// Total bindings in this document.
    std::int32_t count_bindings() const noexcept;
};

// ---------------------------------------------------------------------------
//  Parse errors
// ---------------------------------------------------------------------------

struct LayoutError {
    std::string  instance_pointer;  // JSON Pointer to the offending node
    std::string  message;          // human-readable, actionable
    std::int32_t line{0};         // source line (0 if not available)
};

struct LayoutResult {
    bool ok() const noexcept { return errors.empty(); }
    std::vector<LayoutError> errors;
    std::shared_ptr<const LayoutDocument> document;
};

// ---------------------------------------------------------------------------
//  LayoutInterpreter — parses a layout document into a LayoutDocument
// ---------------------------------------------------------------------------

class LayoutInterpreter {
public:
    /// Validate and parse a layout document from a JSON string.
    /// Validation uses the SchemaValidator to enforce REQ-THM-040 step 6.
    static LayoutResult parse(std::string_view json,
                              const theme::SchemaValidator& validator);

    /// Validate and parse from a file path.
    static LayoutResult load(const std::filesystem::path& path,
                             const theme::SchemaValidator& validator);

private:
    static LayoutResult build(const nlohmann::json& doc,
                              const theme::SchemaValidator& validator);
};

}  // namespace arrow::skin
