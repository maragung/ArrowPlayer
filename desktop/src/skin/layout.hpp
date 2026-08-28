// SPDX-License-Identifier: MPL-2.0
//
// layout.hpp — REQ-THM-025 / REQ-THM-026 / REQ-THM-027 / REQ-THM-028
// / REQ-THM-029 / REQ-THM-030 / REQ-THM-031 / REQ-THM-032 / REQ-THM-033
// / REQ-THM-034.
//
// The layout interpreter is the only thing the desktop and Android UI
// layers share when rendering a skin. Its job is to turn a validated
// .eclayout document into an internal model — the IR — that the
// platform UI (QML on desktop, Compose on Android) consumes.
//
// What this file is NOT:
//   * a scripting engine. There is no eval(), no function dispatch,
//     no expression language. The only "logic" is the `when`
//     predicate, which is a fixed grammar (see parse_when() in
//     layout.cpp).
//   * a QML/JS shim. Nothing here imports Qml. The IR is plain data
//     — variant-like values, enum tags, and string references — and
//     the QML side walks it to build its Item tree. The Android
//     side does the same with Compose.
//   * a styling engine. Colours are referenced by `color.<group>.<role>`
//     string, not by literal hex, because a literal would break
//     every theme the skin is combined with. The host looks the path
//     up on the active theme.
//
// Budgets (REQ-THM-033):
//   * 500 component instances per layout
//   * nesting depth 24
//   * 64 bindings per component tree
//
// The interpreter itself contains no scripting, no QML, no JS.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace arrow::skin {

// ---------------------------------------------------------------------------
//  The closed component vocabulary. The numbers are an internal detail
//  and may be re-ordered between builds; consumers should switch on
//  the enum tag, not the integer value.
// ---------------------------------------------------------------------------

enum class Component : std::uint16_t {
    // Layout (8)
    Stack      = 0,  Row,        Column,     Grid,
    Panel,             Spacer,     ScrollArea, SplitPane,

    // Content (7)
    Text,             Icon,       Image,      Marquee,
    Divider,          Badge,      Rating,     ProgressBar,

    // Interactive (8)
    Button,           ToggleButton, Slider,    VolumeControl,
    TransportBar,     SearchField, TabBar,    ListView,

    // Media (6)
    AlbumArt,         Visualizer, SeekBar,   PeakMeter,
    WaveformView,     LyricsView,
};

// All 30 components, in the same order as REQ-THM-026. Used to validate
// `type` strings when reading a .eclayout document; the schema also
// validates it but the interpreter does not trust the schema alone.
std::optional<Component> component_from_string(std::string_view s);
const char* component_to_string(Component c);

// ---------------------------------------------------------------------------
//  Bindings — REQ-THM-027. The state model is a closed set of paths
//  the host publishes. The interpreter does not look the path up; the
//  host does, when it renders a frame.
// ---------------------------------------------------------------------------

enum class Binding : std::uint16_t {
    // track.*  (19)
    Title, AlbumArtist, Artist, Album, Genre, Composer, Year,
    TrackNumber, DiscNumber, Duration, Rating, IsLoved, HasArtwork,
    HasLyrics, Codec, Bitrate, SampleRate, BitDepth, Channels,
    IsLossless, FileName,

    // player.*  (12)
    State, PositionMs, DurationMs, RemainingMs, Volume, IsMuted,
    RepeatMode, ShuffleMode, Speed, IsBitPerfect, ReplayGainApplied,
    HasNext, HasPrevious,

    // queue.* and list.*  (4)
    QueueIndex, QueueTotal, ListIndex, ListTotal, ListSelectionCount,

    // settings.*  (4) — the narrow window REQ-THM-027 / REQ-THM-031
    // disagree about; layout.schema.json's `statePath` enum is the
    // source of truth, and this list mirrors it.
    ShowMiniVisualizer, ShowVisualizer, ShowLyrics, ReducedMotion,
    AccessibleContrast,

    // app.*  (2)
    AppVersion, IsPortable,
};

std::optional<Binding> binding_from_string(std::string_view s);
const char* binding_to_string(Binding b);

// ---------------------------------------------------------------------------
//  Actions — REQ-THM-028. The action set is the §13.2 command registry
//  exposed through the skin's view of it; the interpreter does not
//  know how to perform any of them, only how to name them so the host
//  can dispatch.
// ---------------------------------------------------------------------------

enum class Action : std::uint16_t {
    PlayPause, Next, Previous,
    SeekForward, SeekBackward,
    SeekForwardLarge, SeekBackwardLarge,
    Stop, StopAfterCurrent,
    VolumeUp, VolumeDown, MuteToggle,
    CycleRepeat, CycleShuffle, AddBookmark,
    AbRepeatSetA, AbRepeatSetB, AbRepeatClear,
    SpeedUp, SpeedDown, SpeedReset,

    FocusSearch, CommandPalette,
    GotoTab1, GotoTab2, GotoTab3, GotoTab4, GotoTab5,
    GotoTab6, GotoTab7, GotoTab8, GotoTab9,
    NextTab, PreviousTab,
    FullScreenNowPlaying, ToggleMiniPlayer, ToggleWindowshade,
    ToggleAlwaysOnTop, ScrollToCurrentTrack, ToggleLyrics,
    ToggleVisualizer, ShowEqualizer, Dismiss,

    OpenFiles, OpenFolder, OpenUrl, DeleteFromDisk,

    NewPlaylist, SavePlaylist, ClosePlaylistTab, SelectAll,
    RemoveSelected, Undo, Redo,

    AddSelected, PlayNext,

    ShowTechnicalInfo, EditTags, ToggleLoved,
    SetRating0, SetRating1, SetRating2, SetRating3, SetRating4, SetRating5,

    RescanAll,

    WindowClose, WindowMinimize, WindowMaximizeRestore,
};

std::optional<Action> action_from_string(std::string_view s);
const char* action_to_string(Action a);

// ---------------------------------------------------------------------------
//  Primitive values the IR carries. Numeric sizes are doubles so a
//  pixel number, a percentage, and the strings "fill" / "auto" can
//  be represented uniformly: the host interprets each Dimension
//  according to its kind.
// ---------------------------------------------------------------------------

enum class SizeKind : std::uint8_t { Pixels, Percent, Fill, Auto };

struct Dimension {
    SizeKind  kind{SizeKind::Pixels};
    double    value{0.0};
    std::string to_string() const;   // for diagnostics, not for round-tripping
};

enum class Align : std::uint8_t { Start, Center, End, Stretch, Baseline };
enum class Justify : std::uint8_t { Start, Center, End, SpaceBetween, SpaceAround };
enum class Orientation : std::uint8_t { Horizontal, Vertical };
enum class Overflow : std::uint8_t { Clip, Ellipsis, Wrap, Marquee };
enum class Fit : std::uint8_t { Cover, Contain, Fill, None };
enum class Fallback : std::uint8_t { Placeholder, None, BlurredColor };

// A `when:` predicate compiled to a flat list of clauses. Predicates
// are deliberately small (REQ-THM-030) so we do not need a full AST;
// the simplest representation that round-trips the grammar is a list
// of (binding, op, literal) atoms joined by and/or with optional not
// at the front of each clause. The host evaluates them at render
// time against the current state.
struct WhenClause {
    bool                              negated{false};
    Binding                           binding;
    enum class Op { Exists, Eq, Ne, Lt, Le, Gt, Ge } op{Op::Exists};
    enum class LiteralKind { Bool, Int, Double, String } kind{LiteralKind::String};
    bool                              bool_value{false};
    std::int64_t                      int_value{0};
    double                            double_value{0.0};
    std::string                       string_value;
};

struct WhenPredicate {
    std::vector<WhenClause> clauses;
    std::vector<std::string> joins;   // one of {"and","or"} between clauses

    // True if the predicate is empty. The host treats an empty
    // predicate as always-true; we mirror that here so a host-side
    // evaluator can `if (!p.empty()) ...`.
    bool empty() const noexcept { return clauses.empty(); }
};

// One TransportBar button id (closed set of 11 names).
enum class TransportButton : std::uint8_t {
    Previous, PlayPause, Play, Pause, Stop, Next,
    Shuffle, Repeat, StopAfterCurrent, Loved, Rating,
};
std::optional<TransportButton> transport_button_from(std::string_view s);

// A type-scale token from the active theme.
enum class TypeStyle : std::uint8_t {
    Display, Headline, Title, Body, Label, Caption, Mono,
};
std::optional<TypeStyle> type_style_from(std::string_view s);

// A visualizer style.
enum class VisualizerStyle : std::uint8_t {
    Bars, Oscilloscope, Spectrum, VuMeter, None,
};
std::optional<VisualizerStyle> visualizer_style_from(std::string_view s);

// ---------------------------------------------------------------------------
//  Node — the IR. A Node is a value type; the tree is owned by
//  LayoutDocument and held by shared_ptr<const LayoutDocument>.
// ---------------------------------------------------------------------------

struct Node {
    Component                                       type;
    std::string                                     id;          // author name
    WhenPredicate                                   when;        // visibility
    WhenPredicate                                   enabled_when;

    // Layout properties
    std::optional<std::string>                      padding;
    std::optional<std::string>                      margin;
    std::optional<std::string>                      spacing;
    Dimension                                       width{SizeKind::Auto, 0.0};
    Dimension                                       height{SizeKind::Auto, 0.0};
    std::optional<Dimension>                        min_width;
    std::optional<Dimension>                        min_height;
    std::optional<Dimension>                        max_width;
    std::optional<Dimension>                        max_height;
    std::optional<double>                           grow;
    std::optional<Align>                            align;
    std::optional<Justify>                          justify;
    std::optional<Orientation>                      orientation;
    std::optional<Overflow>                         overflow;
    std::optional<int>                              max_lines;
    std::optional<std::string>                      text_align;

    // Style
    std::optional<std::string>                      background;   // color.* path
    std::optional<std::string>                      color;        // color.* path
    std::optional<std::string>                      border_color;
    std::optional<double>                           border_width;
    std::optional<int>                              radius;       // 0..9999, or token
    std::optional<int>                              elevation;
    std::optional<double>                           opacity;
    std::optional<bool>                             clip;

    // Content source — exactly one is set on a Text/Marquee; the
    // validator checks the oneOf, the interpreter trusts it.
    std::optional<std::string>                      text;
    std::optional<std::string>                      efs;
    std::optional<Binding>                          bind;
    std::optional<TypeStyle>                        text_style;  // type-scale token
    std::optional<VisualizerStyle>                  visualizer_style;

    // Interactive
    std::optional<Action>                           action;
    std::optional<std::string>                      tooltip;
    std::optional<std::string>                      accessible_name;

    // Asset / icon
    std::optional<std::string>                      source;      // images/foo.png
    std::optional<std::string>                      icon;        // icon name
    std::optional<double>                           icon_size;
    std::optional<Fit>                              fit;
    std::optional<Fallback>                         fallback;

    // Containers
    int                                             columns{0};
    int                                             rows{0};
    std::string                                     scroll_direction; // "vertical"|...
    double                                          split{0.0};

    // TransportBar / ListView / TabBar / Marquee / ProgressBar etc.
    std::vector<TransportButton>                    transport_buttons;
    std::optional<double>                           min_value;
    std::optional<double>                           max_value;
    std::optional<double>                           step;
    std::optional<bool>                             show_buffered;
    std::optional<bool>                             show_labels;
    std::optional<std::string>                      placeholder;
    int                                             bar_count{0};
    std::vector<Node>                                children;     // owned subtree
};

// ---------------------------------------------------------------------------
//  The document
// ---------------------------------------------------------------------------

struct LayoutDocument {
    std::int32_t    schema_version{1};
    std::string     surface;                  // "main-window" | ...
    std::string     description;
    int             min_width{0};
    int             min_height{0};
    std::shared_ptr<Node> root;

    // Stats from the last parse — useful to surface in `tools/theme-validate`.
    int   component_count{0};     // REQ-THM-033 budget 500
    int   max_depth{0};           // budget 24
    int   binding_count{0};       // budget 64
};

// ---------------------------------------------------------------------------
//  Interpret
// ---------------------------------------------------------------------------

enum class LayoutError {
    None,
    ParseError,                 // document is not valid JSON
    SchemaError,                // document is valid JSON but fails the layout schema
    SchemaNotInitialised,
    UnknownComponent,           // type is not in the closed vocabulary
    UnknownAction,              // action is not in the closed enum
    UnknownBinding,             // bind is not a whitelisted state path
    UnknownTypeStyle,            // style is not a type-scale token
    UnknownVisualizerStyle,      // style is not a visualizer style
    UnknownTransportButton,
    UnknownAssetRef,             // source must match images/<name> or icons/<name>
    UnknownIconRef,              // icon must be a kebab-case name
    InvalidDimension,            // out of range
    InvalidSplit,                // not in (0, 1)
    InvalidEfs,                  // EFS pattern failed to parse
    UnknownSpacingToken,         // padding/margin/spacing not in the §12.1 set or numeric
    UnknownRadiusToken,
    UnknownOverflow,
    UnknownAlign,
    UnknownJustify,
    UnknownOrientation,
    UnknownScrollDirection,
    UnknownTextAlign,
    UnknownFit,
    UnknownFallback,
    UnknownColorPath,            // color.* path not in the active theme
    ComponentBudgetExceeded,     // > 500
    DepthBudgetExceeded,         // > 24
    BindingBudgetExceeded,       // > 64
    InvalidPredicate,            // when: not parseable
    InvalidMinSize,
    NodeNotFound,                // referenced node id does not exist
};

struct LayoutResult {
    LayoutError                            error{LayoutError::None};
    std::string                            why;
    std::string                            offending_id;       // for budget errors
    std::string                            offending_pointer;  // JSON Pointer to the node
    std::shared_ptr<LayoutDocument>        document;
};

class LayoutInterpreter {
public:
    explicit LayoutInterpreter(const class theme::SchemaValidator& validator);

    // Parse and interpret one .eclayout document. The validator is
    // used to run the layout schema; the interpreter does not need
    // the theme schema, but it needs a live validator object so we
    // share the same SchemaValidator with the rest of the engine.
    LayoutResult interpret(std::string_view document) const;

    // Build an empty document with the given surface. Useful for
    // unit tests that want a starting point without a JSON file.
    static std::shared_ptr<LayoutDocument> make_empty(const std::string& surface);

    // Parse the `when:` predicate grammar. Exposed for tests; the
    // main pipeline calls it through interpret(). Returns LayoutError
    // on failure and populates `predicate`.
    static LayoutError parse_when(const std::string& src, WhenPredicate& predicate);

    // Validate a single node id (REQ-THM-033 says validation messages
    // name the offending node). The regex is:
    //   ^[A-Za-z][A-Za-z0-9_-]{0,63}$
    static bool is_valid_node_id(const std::string& s);

private:
    const class theme::SchemaValidator* validator_{nullptr};
};

}  // namespace arrow::skin
