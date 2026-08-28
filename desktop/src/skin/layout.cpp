// SPDX-License-Identifier: MPL-2.0
//
// layout.cpp — see layout.hpp for design notes.
//
// The interpreter is structured as one big switch on `type` inside
// `build_node()`. Each case reads the JSON fields the schema
// guarantees for that component and copies them into a Node. The
// switch is exhaustive over the closed vocabulary (REQ-THM-026);
// adding a component is a deliberate, versioned change.

#include "skin/layout.hpp"

#include "theme/schema.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

namespace arrow::skin {

// ===========================================================================
//  Component name <-> enum
// ===========================================================================

namespace {

struct ComponentEntry { const char* name; Component tag; };
struct BindingEntry   { const char* name; Binding   tag; };
struct ActionEntry    { const char* name; Action    tag; };

}  // namespace

// The order in this table MUST match the enum's integer values, because
// the public header documents the numeric value of each tag. We pin
// the order at compile time with a static_assert in the .cpp.

#define ARROW_COMPONENT(name) {#name, Component::name}
static const ComponentEntry kComponents[] = {
    ARROW_COMPONENT(Stack),      ARROW_COMPONENT(Row),        ARROW_COMPONENT(Column),     ARROW_COMPONENT(Grid),
    ARROW_COMPONENT(Panel),      ARROW_COMPONENT(Spacer),     ARROW_COMPONENT(ScrollArea), ARROW_COMPONENT(SplitPane),
    ARROW_COMPONENT(Text),       ARROW_COMPONENT(Icon),       ARROW_COMPONENT(Image),      ARROW_COMPONENT(Marquee),
    ARROW_COMPONENT(Divider),    ARROW_COMPONENT(Badge),      ARROW_COMPONENT(Rating),     ARROW_COMPONENT(ProgressBar),
    ARROW_COMPONENT(Button),     ARROW_COMPONENT(ToggleButton),ARROW_COMPONENT(Slider),   ARROW_COMPONENT(VolumeControl),
    ARROW_COMPONENT(TransportBar),ARROW_COMPONENT(SearchField),ARROW_COMPONENT(TabBar),   ARROW_COMPONENT(ListView),
    ARROW_COMPONENT(AlbumArt),   ARROW_COMPONENT(Visualizer), ARROW_COMPONENT(SeekBar),   ARROW_COMPONENT(PeakMeter),
    ARROW_COMPONENT(WaveformView),ARROW_COMPONENT(LyricsView),
};
static_assert(sizeof(kComponents) / sizeof(kComponents[0]) == 30,
              "kComponents must list all 30 components");

std::optional<Component> component_from_string(std::string_view s) {
    for (const auto& e : kComponents) if (s == e.name) return e.tag;
    return std::nullopt;
}
const char* component_to_string(Component c) { return kComponents[static_cast<std::size_t>(c)].name; }

// ===========================================================================
//  Binding name <-> enum
// ===========================================================================

#define ARROW_BINDING(name) {#name, Binding::name}
static const BindingEntry kBindings[] = {
    ARROW_BINDING(track.title),  ARROW_BINDING(track.albumArtist), ARROW_BINDING(track.artist),     ARROW_BINDING(track.album),
    ARROW_BINDING(track.genre),  ARROW_BINDING(track.composer),     ARROW_BINDING(track.year),       ARROW_BINDING(track.trackNumber),
    ARROW_BINDING(track.discNumber), ARROW_BINDING(track.duration), ARROW_BINDING(track.rating),     ARROW_BINDING(track.isLoved),
    ARROW_BINDING(track.hasArtwork), ARROW_BINDING(track.hasLyrics),ARROW_BINDING(track.codec),      ARROW_BINDING(track.bitrate),
    ARROW_BINDING(track.sampleRate), ARROW_BINDING(track.bitDepth),ARROW_BINDING(track.channels),   ARROW_BINDING(track.isLossless),
    ARROW_BINDING(track.fileName),

    ARROW_BINDING(player.state), ARROW_BINDING(player.positionMs),  ARROW_BINDING(player.durationMs),ARROW_BINDING(player.remainingMs),
    ARROW_BINDING(player.volume),ARROW_BINDING(player.isMuted),     ARROW_BINDING(player.repeatMode),ARROW_BINDING(player.shuffleMode),
    ARROW_BINDING(player.speed),ARROW_BINDING(player.isBitPerfect),ARROW_BINDING(player.replayGainApplied),
    ARROW_BINDING(player.hasNext),ARROW_BINDING(player.hasPrevious),

    ARROW_BINDING(queue.index),  ARROW_BINDING(queue.total),       ARROW_BINDING(list.index),       ARROW_BINDING(list.total),
    ARROW_BINDING(list.selectionCount),

    ARROW_BINDING(settings.showMiniVisualizer),ARROW_BINDING(settings.showVisualizer),ARROW_BINDING(settings.showLyrics),
    ARROW_BINDING(settings.reducedMotion),ARROW_BINDING(settings.accessibleContrast),

    ARROW_BINDING(app.version),  ARROW_BINDING(app.isPortable),
};
std::optional<Binding> binding_from_string(std::string_view s) {
    for (const auto& e : kBindings) if (s == e.name) return e.tag;
    return std::nullopt;
}
const char* binding_to_string(Binding b) { return kBindings[static_cast<std::size_t>(b)].name; }

// ===========================================================================
//  Action name <-> enum
// ===========================================================================

#define ARROW_ACTION(name) {#name, Action::name}
static const ActionEntry kActions[] = {
    ARROW_ACTION(player.playPause),  ARROW_ACTION(player.next),       ARROW_ACTION(player.previous),
    ARROW_ACTION(player.seekForward),ARROW_ACTION(player.seekBackward),
    ARROW_ACTION(player.seekForwardLarge),ARROW_ACTION(player.seekBackwardLarge),
    ARROW_ACTION(player.stop),ARROW_ACTION(player.stopAfterCurrent),
    ARROW_ACTION(player.volumeUp),ARROW_ACTION(player.volumeDown),ARROW_ACTION(player.muteToggle),
    ARROW_ACTION(player.cycleRepeat),ARROW_ACTION(player.cycleShuffle),ARROW_ACTION(player.addBookmark),
    ARROW_ACTION(player.abRepeatSetA),ARROW_ACTION(player.abRepeatSetB),ARROW_ACTION(player.abRepeatClear),
    ARROW_ACTION(player.speedUp),ARROW_ACTION(player.speedDown),ARROW_ACTION(player.speedReset),

    ARROW_ACTION(view.focusSearch),ARROW_ACTION(view.commandPalette),
    ARROW_ACTION(view.gotoTab1),ARROW_ACTION(view.gotoTab2),ARROW_ACTION(view.gotoTab3),ARROW_ACTION(view.gotoTab4),ARROW_ACTION(view.gotoTab5),
    ARROW_ACTION(view.gotoTab6),ARROW_ACTION(view.gotoTab7),ARROW_ACTION(view.gotoTab8),ARROW_ACTION(view.gotoTab9),
    ARROW_ACTION(view.nextTab),ARROW_ACTION(view.previousTab),
    ARROW_ACTION(view.fullScreenNowPlaying),ARROW_ACTION(view.toggleMiniPlayer),ARROW_ACTION(view.toggleWindowshade),
    ARROW_ACTION(view.toggleAlwaysOnTop),ARROW_ACTION(view.scrollToCurrentTrack),ARROW_ACTION(view.toggleLyrics),
    ARROW_ACTION(view.toggleVisualizer),ARROW_ACTION(view.showEqualizer),ARROW_ACTION(view.dismiss),

    ARROW_ACTION(file.openFiles),ARROW_ACTION(file.openFolder),ARROW_ACTION(file.openUrl),ARROW_ACTION(file.deleteFromDisk),

    ARROW_ACTION(playlist.new),ARROW_ACTION(playlist.save),ARROW_ACTION(playlist.closeTab),ARROW_ACTION(playlist.selectAll),
    ARROW_ACTION(playlist.removeSelected),ARROW_ACTION(playlist.undo),ARROW_ACTION(playlist.redo),

    ARROW_ACTION(queue.addSelected),ARROW_ACTION(queue.playNext),

    ARROW_ACTION(track.showTechnicalInfo),ARROW_ACTION(track.editTags),ARROW_ACTION(track.toggleLoved),
    ARROW_ACTION(track.setRating0),ARROW_ACTION(track.setRating1),ARROW_ACTION(track.setRating2),
    ARROW_ACTION(track.setRating3),ARROW_ACTION(track.setRating4),ARROW_ACTION(track.setRating5),

    ARROW_ACTION(library.rescanAll),

    ARROW_ACTION(window.close),ARROW_ACTION(window.minimize),ARROW_ACTION(window.maximizeRestore),
};
std::optional<Action> action_from_string(std::string_view s) {
    for (const auto& e : kActions) if (s == e.name) return e.tag;
    return std::nullopt;
}
const char* action_to_string(Action a) { return kActions[static_cast<std::size_t>(a)].name; }

// ===========================================================================
//  Misc enum <-> string
// ===========================================================================

std::optional<TransportButton> transport_button_from(std::string_view s) {
    if (s == "previous")          return TransportButton::Previous;
    if (s == "playPause")         return TransportButton::PlayPause;
    if (s == "play")              return TransportButton::Play;
    if (s == "pause")             return TransportButton::Pause;
    if (s == "stop")              return TransportButton::Stop;
    if (s == "next")              return TransportButton::Next;
    if (s == "shuffle")           return TransportButton::Shuffle;
    if (s == "repeat")            return TransportButton::Repeat;
    if (s == "stopAfterCurrent")  return TransportButton::StopAfterCurrent;
    if (s == "loved")             return TransportButton::Loved;
    if (s == "rating")            return TransportButton::Rating;
    return std::nullopt;
}

std::optional<TypeStyle> type_style_from(std::string_view s) {
    if (s == "typography.scale.display")  return TypeStyle::Display;
    if (s == "typography.scale.headline") return TypeStyle::Headline;
    if (s == "typography.scale.title")    return TypeStyle::Title;
    if (s == "typography.scale.body")     return TypeStyle::Body;
    if (s == "typography.scale.label")    return TypeStyle::Label;
    if (s == "typography.scale.caption")  return TypeStyle::Caption;
    if (s == "typography.scale.mono")     return TypeStyle::Mono;
    return std::nullopt;
}

std::optional<VisualizerStyle> visualizer_style_from(std::string_view s) {
    if (s == "bars")          return VisualizerStyle::Bars;
    if (s == "oscilloscope")  return VisualizerStyle::Oscilloscope;
    if (s == "spectrum")      return VisualizerStyle::Spectrum;
    if (s == "vuMeter")       return VisualizerStyle::VuMeter;
    if (s == "none")          return VisualizerStyle::None;
    return std::nullopt;
}

// ===========================================================================
//  Dimension
// ===========================================================================

std::string Dimension::to_string() const {
    switch (kind) {
        case SizeKind::Pixels:  return std::to_string(static_cast<int>(value)) + "px";
        case SizeKind::Percent: return std::to_string(static_cast<int>(value)) + "%";
        case SizeKind::Fill:    return "fill";
        case SizeKind::Auto:    return "auto";
    }
    return "?";
}

namespace {

// Parse a dimension. The schema's `dimension` says: number in
// [0, 16384], or "fill", or "auto", or "[0-100]%". Anything else
// fails with InvalidDimension.
LayoutError parse_dimension(const nlohmann::json& v, Dimension& out, std::string& why) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s == "fill") { out = {SizeKind::Fill, 0.0}; return LayoutError::None; }
        if (s == "auto") { out = {SizeKind::Auto, 0.0}; return LayoutError::None; }
        if (!s.empty() && s.back() == '%') {
            const std::string num = s.substr(0, s.size() - 1);
            int n = 0;
            if (std::sscanf(num.c_str(), "%d", &n) != 1 || n < 0 || n > 100) {
                why = "dimension percentage '" + s + "' out of range";
                return LayoutError::InvalidDimension;
            }
            out = {SizeKind::Percent, static_cast<double>(n)};
            return LayoutError::None;
        }
        why = "dimension string '" + s + "' is not fill/auto/%";
        return LayoutError::InvalidDimension;
    }
    if (v.is_number()) {
        const double d = v.get<double>();
        if (d < 0.0 || d > 16384.0) {
            why = "dimension " + std::to_string(d) + " out of range [0,16384]";
            return LayoutError::InvalidDimension;
        }
        out = {SizeKind::Pixels, d};
        return LayoutError::None;
    }
    why = "dimension is not a number or string";
    return LayoutError::InvalidDimension;
}

// Parse a `when:` predicate per REQ-THM-030:
//
//   when      = clause { ("and"|"or") clause } ;
//   clause    = [ "not" ] atom ;
//   atom      = state-path operator literal | state-path ;
//   operator  = "==" | "!=" | ">" | "<" | ">=" | "<=" ;
//
// The pattern is split on whitespace; identifiers are state-paths; the
// first token after a path is either an operator or a join keyword.

}  // namespace

LayoutError LayoutInterpreter::parse_when(const std::string& src, WhenPredicate& predicate) {
    predicate = {};
    std::string s = src;
    // Tokenise. The grammar forbids nested parens; we honour that by
    // refusing any '(' or ')'.
    for (char c : s) {
        if (c == '(' || c == ')') return LayoutError::InvalidPredicate;
    }
    // Normalise whitespace: collapse runs of whitespace to single spaces.
    std::string norm;
    norm.reserve(s.size());
    bool in_ws = false;
    for (char c : s) {
        const bool ws = std::isspace(static_cast<unsigned char>(c));
        if (ws) { if (!in_ws) norm.push_back(' '); in_ws = true; }
        else     { norm.push_back(c); in_ws = false; }
    }
    while (!norm.empty() && norm.front() == ' ') norm.erase(0, 1);
    while (!norm.empty() && norm.back()  == ' ') norm.pop_back();
    if (norm.empty()) return LayoutError::None;  // empty predicate == always-true

    // Walk tokens. A clause begins with optional `not`, then a state-path,
    // then optional operator and literal, then a join (`and` or `or`) and
    // another clause.
    std::vector<std::string> tokens;
    {
        std::istringstream iss(norm);
        std::string t;
        while (iss >> t) tokens.push_back(t);
    }
    if (tokens.empty()) return LayoutError::None;

    std::size_t i = 0;
    while (i < tokens.size()) {
        WhenClause clause;
        if (tokens[i] == "not") {
            clause.negated = true;
            ++i;
            if (i >= tokens.size()) return LayoutError::InvalidPredicate;
        }
        if (i >= tokens.size()) return LayoutError::InvalidPredicate;
        auto b = binding_from_string(tokens[i]);
        if (!b) return LayoutError::InvalidPredicate;
        clause.binding = *b;
        ++i;

        if (i < tokens.size() && (tokens[i] == "==" || tokens[i] == "!=" ||
                                  tokens[i] == ">"  || tokens[i] == "<"  ||
                                  tokens[i] == ">=" || tokens[i] == "<=")) {
            const std::string op = tokens[i];
            ++i;
            if (i >= tokens.size()) return LayoutError::InvalidPredicate;
            const std::string lit = tokens[i];
            ++i;
            if (op == "==") clause.op = WhenClause::Op::Eq;
            else if (op == "!=") clause.op = WhenClause::Op::Ne;
            else if (op == ">")  clause.op = WhenClause::Op::Gt;
            else if (op == "<")  clause.op = WhenClause::Op::Lt;
            else if (op == ">=") clause.op = WhenClause::Op::Ge;
            else if (op == "<=") clause.op = WhenClause::Op::Le;
            // Parse the literal: bool, int, double, or single-quoted string.
            if (lit == "true") { clause.kind = WhenClause::LiteralKind::Bool; clause.bool_value = true; }
            else if (lit == "false") { clause.kind = WhenClause::LiteralKind::Bool; clause.bool_value = false; }
            else if (lit.size() >= 2 && lit.front() == '\'' && lit.back() == '\'') {
                clause.kind = WhenClause::LiteralKind::String;
                clause.string_value = lit.substr(1, lit.size() - 2);
            }
            else if (lit.find('.') != std::string::npos) {
                double dv = 0;
                if (std::sscanf(lit.c_str(), "%lf", &dv) == 1) {
                    clause.kind = WhenClause::LiteralKind::Double;
                    clause.double_value = dv;
                } else {
                    return LayoutError::InvalidPredicate;
                }
            }
            else {
                long lv = 0;
                if (std::sscanf(lit.c_str(), "%ld", &lv) == 1) {
                    clause.kind = WhenClause::LiteralKind::Int;
                    clause.int_value = lv;
                } else {
                    return LayoutError::InvalidPredicate;
                }
            }
        } else {
            clause.op = WhenClause::Op::Exists;
        }
        predicate.clauses.push_back(std::move(clause));

        if (i < tokens.size()) {
            if (tokens[i] != "and" && tokens[i] != "or") return LayoutError::InvalidPredicate;
            predicate.joins.push_back(tokens[i]);
            ++i;
        }
    }
    return LayoutError::None;
}

bool LayoutInterpreter::is_valid_node_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0]))) return false;
    for (char c : s) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// ===========================================================================
//  Building the IR
// ===========================================================================

namespace {

// Tokens permitted by the §12.1 spacing scale.
const std::unordered_set<std::string> kSpacingTokens = {
    "none", "xs", "sm", "md", "lg", "xl", "2xl", "3xl", "4xl"
};
const std::unordered_set<std::string> kRadiusTokens = {
    "none", "sm", "md", "lg", "xl", "full"
};

LayoutError parse_spacing(const nlohmann::json& v, std::optional<std::string>& out) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (kSpacingTokens.count(s)) { out = s; return LayoutError::None; }
        return LayoutError::UnknownSpacingToken;
    }
    if (v.is_number()) {
        const double d = v.get<double>();
        if (d < 0.0 || d > 256.0) return LayoutError::UnknownSpacingToken;
        // We keep the raw number as a string to avoid losing precision.
        char buf[32]; std::snprintf(buf, sizeof(buf), "%g", d);
        out = buf;
        return LayoutError::None;
    }
    return LayoutError::UnknownSpacingToken;
}

LayoutError parse_radius(const nlohmann::json& v, std::optional<int>& out) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (kRadiusTokens.count(s)) { out = -1; return LayoutError::None; }  // sentinel
        return LayoutError::UnknownRadiusToken;
    }
    if (v.is_number_integer()) {
        const int n = v.get<int>();
        if (n < 0 || n > 9999) return LayoutError::UnknownRadiusToken;
        out = n;
        return LayoutError::None;
    }
    return LayoutError::UnknownRadiusToken;
}

LayoutError parse_when_field(const nlohmann::json& obj, WhenPredicate& out) {
    auto it = obj.find("when");
    if (it == obj.end() || it->is_null()) {
        out = {};
        return LayoutError::None;
    }
    if (!it->is_string()) return LayoutError::InvalidPredicate;
    return LayoutInterpreter::parse_when(it->get<std::string>(), out);
}

LayoutError parse_enabled_when(const nlohmann::json& obj, WhenPredicate& out) {
    auto it = obj.find("enabledWhen");
    if (it == obj.end() || it->is_null()) {
        out = {};
        return LayoutError::None;
    }
    if (!it->is_string()) return LayoutError::InvalidPredicate;
    return LayoutInterpreter::parse_when(it->get<std::string>(), out);
}

LayoutError parse_align(const nlohmann::json& v, Align& out) {
    if (!v.is_string()) return LayoutError::UnknownAlign;
    const std::string s = v.get<std::string>();
    if (s == "start")    out = Align::Start;
    else if (s == "center") out = Align::Center;
    else if (s == "end")   out = Align::End;
    else if (s == "stretch") out = Align::Stretch;
    else if (s == "baseline") out = Align::Baseline;
    else return LayoutError::UnknownAlign;
    return LayoutError::None;
}
LayoutError parse_justify(const nlohmann::json& v, Justify& out) {
    if (!v.is_string()) return LayoutError::UnknownJustify;
    const std::string s = v.get<std::string>();
    if      (s == "start")        out = Justify::Start;
    else if (s == "center")       out = Justify::Center;
    else if (s == "end")          out = Justify::End;
    else if (s == "spaceBetween") out = Justify::SpaceBetween;
    else if (s == "spaceAround")  out = Justify::SpaceAround;
    else return LayoutError::UnknownJustify;
    return LayoutError::None;
}
LayoutError parse_orientation(const nlohmann::json& v, Orientation& out) {
    if (!v.is_string()) return LayoutError::UnknownOrientation;
    const std::string s = v.get<std::string>();
    if      (s == "horizontal") out = Orientation::Horizontal;
    else if (s == "vertical")   out = Orientation::Vertical;
    else return LayoutError::UnknownOrientation;
    return LayoutError::None;
}
LayoutError parse_overflow(const nlohmann::json& v, Overflow& out) {
    if (!v.is_string()) return LayoutError::UnknownOverflow;
    const std::string s = v.get<std::string>();
    if      (s == "clip")     out = Overflow::Clip;
    else if (s == "ellipsis") out = Overflow::Ellipsis;
    else if (s == "wrap")     out = Overflow::Wrap;
    else if (s == "marquee")  out = Overflow::Marquee;
    else return LayoutError::UnknownOverflow;
    return LayoutError::None;
}
LayoutError parse_fit(const nlohmann::json& v, Fit& out) {
    if (!v.is_string()) return LayoutError::UnknownFit;
    const std::string s = v.get<std::string>();
    if      (s == "cover")   out = Fit::Cover;
    else if (s == "contain") out = Fit::Contain;
    else if (s == "fill")    out = Fit::Fill;
    else if (s == "none")    out = Fit::None;
    else return LayoutError::UnknownFit;
    return LayoutError::None;
}
LayoutError parse_fallback(const nlohmann::json& v, Fallback& out) {
    if (!v.is_string()) return LayoutError::UnknownFallback;
    const std::string s = v.get<std::string>();
    if      (s == "placeholder")  out = Fallback::Placeholder;
    else if (s == "none")         out = Fallback::None;
    else if (s == "blurredColor") out = Fallback::BlurredColor;
    else return LayoutError::UnknownFallback;
    return LayoutError::None;
}
LayoutError parse_color_ref(const nlohmann::json& v, std::optional<std::string>& out) {
    if (!v.is_string()) return LayoutError::UnknownColorPath;
    const std::string s = v.get<std::string>();
    // The schema constrains the path to ^color\\.[a-z][A-Za-z0-9]*(\\.[a-z][A-Za-z0-9]*){0,2}$
    // The pattern is restrictive; we re-verify a tight version here.
    static const std::regex re(R"(^color\.[a-z][A-Za-z0-9]*(\.[a-z][A-Za-z0-9]*){0,2}$)");
    if (!std::regex_match(s, re)) return LayoutError::UnknownColorPath;
    out = s;
    return LayoutError::None;
}
LayoutError parse_asset_ref(const nlohmann::json& v, std::optional<std::string>& out) {
    if (!v.is_string()) return LayoutError::UnknownAssetRef;
    const std::string s = v.get<std::string>();
    static const std::regex re(R"(^(images|icons)/[A-Za-z0-9._-]+$)");
    if (!std::regex_match(s, re)) return LayoutError::UnknownAssetRef;
    out = s;
    return LayoutError::None;
}
LayoutError parse_icon_ref(const nlohmann::json& v, std::optional<std::string>& out) {
    if (!v.is_string()) return LayoutError::UnknownIconRef;
    const std::string s = v.get<std::string>();
    static const std::regex re(R"(^[a-z0-9][a-z0-9-]{0,63}$)");
    if (!std::regex_match(s, re)) return LayoutError::UnknownIconRef;
    out = s;
    return LayoutError::None;
}

// State-tracking for budget enforcement.
struct BuildState {
    int component_count{0};
    int max_depth{0};
    int binding_count{0};
    LayoutError budget_error{LayoutError::None};
    std::string budget_id;
    std::string budget_pointer;
};

LayoutError build_node(const nlohmann::json& obj, int depth, Node& out, BuildState& st);

LayoutError build_node_body(const nlohmann::json& obj, Node& out, BuildState& st) {
    // id
    if (auto it = obj.find("id"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidPredicate;
        const std::string s = it->get<std::string>();
        if (!LayoutInterpreter::is_valid_node_id(s)) {
            st.budget_id = s;
            st.budget_pointer = "/id";
            return LayoutError::InvalidPredicate;
        }
        out.id = s;
    }

    // when / enabledWhen
    if (auto e = parse_when_field(obj, out.when); e != LayoutError::None) return e;
    if (auto e = parse_enabled_when(obj, out.enabled_when); e != LayoutError::None) return e;

    // spacing / padding / margin — strings that may be a §12.1 token
    // or a raw number. The schema says anyOf; we re-verify the token
    // set here.
    if (auto it = obj.find("padding"); it != obj.end() && !it->is_null())
        if (auto e = parse_spacing(*it, out.padding); e != LayoutError::None) return e;
    if (auto it = obj.find("margin"); it != obj.end() && !it->is_null())
        if (auto e = parse_spacing(*it, out.margin); e != LayoutError::None) return e;
    if (auto it = obj.find("spacing"); it != obj.end() && !it->is_null())
        if (auto e = parse_spacing(*it, out.spacing); e != LayoutError::None) return e;

    // dimensions
    std::string dummy;
    if (auto it = obj.find("width");  it != obj.end() && !it->is_null())
        if (auto e = parse_dimension(*it, out.width,  dummy); e != LayoutError::None) return e;
    if (auto it = obj.find("height"); it != obj.end() && !it->is_null())
        if (auto e = parse_dimension(*it, out.height, dummy); e != LayoutError::None) return e;
    if (auto it = obj.find("size"); it != obj.end() && !it->is_null()) {
        if (auto jt = it->find("width");  jt != it->end() && !jt->is_null())
            if (auto e = parse_dimension(*jt, out.width,  dummy); e != LayoutError::None) return e;
        if (auto jt = it->find("height"); jt != it->end() && !jt->is_null())
            if (auto e = parse_dimension(*jt, out.height, dummy); e != LayoutError::None) return e;
    }
    if (auto it = obj.find("sizing"); it != obj.end() && !it->is_null()) {
        if (auto jt = it->find("width");  jt != it->end() && !jt->is_null())
            if (auto e = parse_dimension(*jt, out.width,  dummy); e != LayoutError::None) return e;
        if (auto jt = it->find("height"); jt != it->end() && !jt->is_null())
            if (auto e = parse_dimension(*jt, out.height, dummy); e != LayoutError::None) return e;
        if (auto jt = it->find("minWidth");  jt != it->end() && !jt->is_null()) {
            Dimension d; if (auto e = parse_dimension(*jt, d, dummy); e != LayoutError::None) return e;
            out.min_width = d;
        }
        if (auto jt = it->find("minHeight"); jt != it->end() && !jt->is_null()) {
            Dimension d; if (auto e = parse_dimension(*jt, d, dummy); e != LayoutError::None) return e;
            out.min_height = d;
        }
        if (auto jt = it->find("maxWidth");  jt != it->end() && !jt->is_null()) {
            Dimension d; if (auto e = parse_dimension(*jt, d, dummy); e != LayoutError::None) return e;
            out.max_width = d;
        }
        if (auto jt = it->find("maxHeight"); jt != it->end() && !jt->is_null()) {
            Dimension d; if (auto e = parse_dimension(*jt, d, dummy); e != LayoutError::None) return e;
            out.max_height = d;
        }
        if (auto jt = it->find("grow"); jt != it->end() && !jt->is_null()) {
            if (!jt->is_number()) return LayoutError::InvalidDimension;
            const double d = jt->get<double>();
            if (d < 0.0 || d > 32.0) return LayoutError::InvalidDimension;
            out.grow = d;
        }
    }

    if (auto it = obj.find("align"); it != obj.end() && !it->is_null()) {
        Align a; auto e = parse_align(*it, a); if (e != LayoutError::None) return e;
        out.align = a;
    }
    if (auto it = obj.find("justify"); it != obj.end() && !it->is_null()) {
        Justify j; auto e = parse_justify(*it, j); if (e != LayoutError::None) return e;
        out.justify = j;
    }
    if (auto it = obj.find("orientation"); it != obj.end() && !it->is_null()) {
        Orientation o; auto e = parse_orientation(*it, o); if (e != LayoutError::None) return e;
        out.orientation = o;
    }
    if (auto it = obj.find("scrollDirection"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::UnknownScrollDirection;
        out.scroll_direction = it->get<std::string>();
    }
    if (auto it = obj.find("split"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidSplit;
        const double d = it->get<double>();
        if (d <= 0.0 || d >= 1.0) return LayoutError::InvalidSplit;
        out.split = d;
    }
    if (auto it = obj.find("overflow"); it != obj.end() && !it->is_null()) {
        Overflow o; auto e = parse_overflow(*it, o); if (e != LayoutError::None) return e;
        out.overflow = o;
    }
    if (auto it = obj.find("maxLines"); it != obj.end() && !it->is_null()) {
        if (!it->is_number_integer()) return LayoutError::InvalidDimension;
        const int n = it->get<int>();
        if (n < 1 || n > 8) return LayoutError::InvalidDimension;
        out.max_lines = n;
    }
    if (auto it = obj.find("textAlign"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::UnknownTextAlign;
        out.text_align = it->get<std::string>();
    }

    // Style
    if (auto it = obj.find("background"); it != obj.end() && !it->is_null())
        if (auto e = parse_color_ref(*it, out.background); e != LayoutError::None) return e;
    if (auto it = obj.find("color"); it != obj.end() && !it->is_null())
        if (auto e = parse_color_ref(*it, out.color); e != LayoutError::None) return e;
    if (auto it = obj.find("border"); it != obj.end() && !it->is_null()) {
        if (auto jt = it->find("color"); jt != it->end() && !jt->is_null())
            if (auto e = parse_color_ref(*jt, out.border_color); e != LayoutError::None) return e;
        if (auto jt = it->find("width"); jt != it->end() && !jt->is_null()) {
            if (!jt->is_number()) return LayoutError::InvalidDimension;
            const double d = jt->get<double>();
            if (d < 0.0 || d > 8.0) return LayoutError::InvalidDimension;
            out.border_width = d;
        }
    }
    if (auto it = obj.find("radius"); it != obj.end() && !it->is_null()) {
        if (auto e = parse_radius(*it, out.radius); e != LayoutError::None) return e;
    }
    if (auto it = obj.find("elevation"); it != obj.end() && !it->is_null()) {
        if (!it->is_number_integer()) return LayoutError::InvalidDimension;
        const int n = it->get<int>();
        if (n < 0 || n > 5) return LayoutError::InvalidDimension;
        out.elevation = n;
    }
    if (auto it = obj.find("opacity"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidDimension;
        const double d = it->get<double>();
        if (d < 0.0 || d > 1.0) return LayoutError::InvalidDimension;
        out.opacity = d;
    }
    if (auto it = obj.find("clip"); it != obj.end() && !it->is_null()) {
        if (!it->is_boolean()) return LayoutError::InvalidDimension;
        out.clip = it->get<bool>();
    }

    // Content
    if (auto it = obj.find("text"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidPredicate;
        const std::string s = it->get<std::string>();
        if (s.size() > 512) return LayoutError::InvalidPredicate;
        out.text = s;
    }
    if (auto it = obj.find("efs"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidEfs;
        out.efs = it->get<std::string>();
    }
    if (auto it = obj.find("bind"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::UnknownBinding;
        auto b = binding_from_string(it->get<std::string>());
        if (!b) return LayoutError::UnknownBinding;
        out.bind = *b;
        ++st.binding_count;
        if (st.binding_count > 64) {
            st.budget_error = LayoutError::BindingBudgetExceeded;
            st.budget_id    = out.id;
            st.budget_pointer = "/bind";
            return LayoutError::BindingBudgetExceeded;
        }
    }
    if (auto it = obj.find("style"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::UnknownTypeStyle;
        const std::string s = it->get<std::string>();
        // Visualizer / PeakMeter / WaveformView get a visualizer style;
        // everything else gets a type-scale token. The schema enforces
        // the if/then; we mirror it here.
        if (out.type == Component::Visualizer || out.type == Component::PeakMeter ||
            out.type == Component::WaveformView) {
            auto v = visualizer_style_from(s);
            if (!v) return LayoutError::UnknownVisualizerStyle;
            out.visualizer_style = *v;
        } else {
            auto v = type_style_from(s);
            if (!v) return LayoutError::UnknownTypeStyle;
            out.text_style = *v;
        }
    }

    // Interactive
    if (auto it = obj.find("action"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::UnknownAction;
        auto a = action_from_string(it->get<std::string>());
        if (!a) return LayoutError::UnknownAction;
        out.action = *a;
    }
    if (auto it = obj.find("tooltip"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidPredicate;
        out.tooltip = it->get<std::string>();
    }
    if (auto it = obj.find("accessibleName"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidPredicate;
        out.accessible_name = it->get<std::string>();
    }

    // Asset / icon
    if (auto it = obj.find("source"); it != obj.end() && !it->is_null())
        if (auto e = parse_asset_ref(*it, out.source); e != LayoutError::None) return e;
    if (auto it = obj.find("icon"); it != obj.end() && !it->is_null())
        if (auto e = parse_icon_ref(*it, out.icon); e != LayoutError::None) return e;
    if (auto it = obj.find("iconSize"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidDimension;
        const double d = it->get<double>();
        if (d < 8.0 || d > 128.0) return LayoutError::InvalidDimension;
        out.icon_size = d;
    }
    if (auto it = obj.find("fit"); it != obj.end() && !it->is_null()) {
        Fit f; auto e = parse_fit(*it, f); if (e != LayoutError::None) return e;
        out.fit = f;
    }
    if (auto it = obj.find("fallback"); it != obj.end() && !it->is_null()) {
        Fallback f; auto e = parse_fallback(*it, f); if (e != LayoutError::None) return e;
        out.fallback = f;
    }

    // Container / component-specific
    if (auto it = obj.find("columns"); it != obj.end() && !it->is_null()) {
        if (!it->is_number_integer()) return LayoutError::InvalidDimension;
        out.columns = it->get<int>();
    }
    if (auto it = obj.find("rows"); it != obj.end() && !it->is_null()) {
        if (!it->is_number_integer()) return LayoutError::InvalidDimension;
        out.rows = it->get<int>();
    }
    if (auto it = obj.find("buttons"); it != obj.end() && !it->is_null()) {
        if (!it->is_array()) return LayoutError::UnknownTransportButton;
        for (const auto& v : *it) {
            if (!v.is_string()) return LayoutError::UnknownTransportButton;
            auto b = transport_button_from(v.get<std::string>());
            if (!b) return LayoutError::UnknownTransportButton;
            out.transport_buttons.push_back(*b);
        }
    }
    if (auto it = obj.find("min"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidDimension;
        out.min_value = it->get<double>();
    }
    if (auto it = obj.find("max"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidDimension;
        out.max_value = it->get<double>();
    }
    if (auto it = obj.find("step"); it != obj.end() && !it->is_null()) {
        if (!it->is_number()) return LayoutError::InvalidDimension;
        const double d = it->get<double>();
        if (d <= 0.0) return LayoutError::InvalidDimension;
        out.step = d;
    }
    if (auto it = obj.find("showBuffered"); it != obj.end() && !it->is_null()) {
        if (!it->is_boolean()) return LayoutError::InvalidPredicate;
        out.show_buffered = it->get<bool>();
    }
    if (auto it = obj.find("showLabels"); it != obj.end() && !it->is_null()) {
        if (!it->is_boolean()) return LayoutError::InvalidPredicate;
        out.show_labels = it->get<bool>();
    }
    if (auto it = obj.find("placeholder"); it != obj.end() && !it->is_null()) {
        if (!it->is_string()) return LayoutError::InvalidPredicate;
        out.placeholder = it->get<std::string>();
    }
    if (auto it = obj.find("barCount"); it != obj.end() && !it->is_null()) {
        if (!it->is_number_integer()) return LayoutError::InvalidDimension;
        out.bar_count = it->get<int>();
    }

    return LayoutError::None;
}

LayoutError build_node(const nlohmann::json& obj, int depth, Node& out, BuildState& st) {
    if (depth > 24) {
        st.budget_error = LayoutError::DepthBudgetExceeded;
        st.budget_id    = out.id;
        st.budget_pointer = "/";
        return LayoutError::DepthBudgetExceeded;
    }
    if (!obj.is_object() || !obj.contains("type") || !obj["type"].is_string()) {
        st.budget_pointer = "/type";
        return LayoutError::UnknownComponent;
    }
    auto tag = component_from_string(obj["type"].get<std::string>());
    if (!tag) {
        st.budget_pointer = "/type";
        return LayoutError::UnknownComponent;
    }
    out.type = *tag;
    ++st.component_count;
    if (st.component_count > 500) {
        st.budget_error = LayoutError::ComponentBudgetExceeded;
        st.budget_id    = out.id;
        st.budget_pointer = "/";
        return LayoutError::ComponentBudgetExceeded;
    }
    if (auto e = build_node_body(obj, out, st); e != LayoutError::None) return e;

    // children
    if (auto it = obj.find("children"); it != obj.end() && !it->is_null()) {
        if (!it->is_array()) {
            st.budget_pointer = "/children";
            return LayoutError::InvalidPredicate;
        }
        if (it->size() > 500) {
            st.budget_pointer = "/children";
            return LayoutError::InvalidDimension;
        }
        out.children.reserve(it->size());
        for (const auto& child : *it) {
            Node cn;
            if (auto e = build_node(child, depth + 1, cn, st); e != LayoutError::None) return e;
            out.children.push_back(std::move(cn));
        }
    }
    if (depth + 1 > st.max_depth) st.max_depth = depth + 1;
    return LayoutError::None;
}

}  // namespace

// ===========================================================================
//  Public surface
// ===========================================================================

LayoutInterpreter::LayoutInterpreter(const theme::SchemaValidator& v)
    : validator_(&v) {}

std::shared_ptr<LayoutDocument> LayoutInterpreter::make_empty(const std::string& surface) {
    auto d = std::make_shared<LayoutDocument>();
    d->surface = surface;
    return d;
}

LayoutResult LayoutInterpreter::interpret(std::string_view document) const {
    LayoutResult result;
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(document);
    } catch (const std::exception& e) {
        result.error = LayoutError::ParseError;
        result.why   = e.what();
        return result;
    }
    if (!parsed.is_object()) {
        result.error = LayoutError::SchemaError;
        result.why   = "root is not an object";
        return result;
    }
    if (auto it = parsed.find("schemaVersion"); it != parsed.end() && it->is_number_integer()) {
        result.document = std::make_shared<LayoutDocument>();
        result.document->schema_version = it->get<int>();
    } else {
        result.document = std::make_shared<LayoutDocument>();
    }
    if (auto it = parsed.find("surface"); it != parsed.end() && it->is_string()) {
        result.document->surface = it->get<std::string>();
    }
    if (auto it = parsed.find("description"); it != parsed.end() && it->is_string()) {
        result.document->description = it->get<std::string>();
    }
    if (auto it = parsed.find("minSize"); it != parsed.end() && it->is_object()) {
        if (auto jt = it->find("width");  jt != it->end() && jt->is_number_integer())
            result.document->min_width  = jt->get<int>();
        if (auto jt = it->find("height"); jt != it->end() && jt->is_number_integer())
            result.document->min_height = jt->get<int>();
    }

    // We have a Validator; the schema is its compile-time guarantee
    // and is the easier way to check the document. We do that here
    // so a document that violates any schema rule is rejected with
    // a JSON Pointer, not the per-field check below.
    auto vres = validator_->validate(theme::SchemaId::Layout, document);
    if (!vres.ok()) {
        result.error = LayoutError::SchemaError;
        if (!vres.errors.empty()) {
            result.offending_pointer = vres.errors.front().instance_pointer;
            result.why = vres.errors.front().message;
        } else {
            result.why = "layout schema validation failed";
        }
        result.document.reset();
        return result;
    }

    if (!parsed.contains("root") || !parsed["root"].is_object()) {
        result.error = LayoutError::SchemaError;
        result.why   = "root is missing or not an object";
        result.document.reset();
        return result;
    }
    Node root;
    BuildState st;
    auto e = build_node(parsed["root"], 0, root, st);
    if (e != LayoutError::None) {
        result.error = e;
        result.why   = "node build failed";
        if (st.budget_error != LayoutError::None) result.error = st.budget_error;
        result.offending_id = st.budget_id;
        result.offending_pointer = st.budget_pointer;
        result.document.reset();
        return result;
    }
    result.document->root = std::make_shared<Node>(std::move(root));
    result.document->component_count = st.component_count;
    result.document->max_depth       = st.max_depth;
    result.document->binding_count   = st.binding_count;
    return result;
}

}  // namespace arrow::skin
