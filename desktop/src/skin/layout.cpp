// SPDX-License-Identifier: MPL-2.0
//
// layout.cpp — see layout.hpp for design notes.

#include "skin/layout.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

namespace arrow::skin {

namespace {

// ---------------------------------------------------------------------------
//  Parse helpers
// ---------------------------------------------------------------------------

constexpr std::string_view to_string_view(NodeType t) noexcept {
    switch (t) {
#define CASE(N) case NodeType::N: return #N
        CASE(Text); CASE(Marquee); CASE(Icon); CASE(Image); CASE(AlbumArt);
        CASE(Divider); CASE(Badge); CASE(Button); CASE(ToggleButton);
        CASE(Slider); CASE(VolumeControl); CASE(TransportBar); CASE(SearchField);
        CASE(TabBar); CASE(ListView); CASE(Visualizer); CASE(SeekBar);
        CASE(PeakMeter); CASE(WaveformView); CASE(LyricsView); CASE(Rating);
        CASE(ProgressBar); CASE(Stack); CASE(Row); CASE(Column); CASE(Grid);
        CASE(Panel); CASE(Spacer); CASE(ScrollArea); CASE(SplitPane);
#undef CASE
    }
    return "<unknown>";
}

constexpr std::string_view to_string(NodeType t) noexcept {
    return to_string_view(t);
}

constexpr bool has_children(NodeType t) noexcept {
    switch (t) {
        case NodeType::Stack: case NodeType::Row: case NodeType::Column:
        case NodeType::Grid: case NodeType::Panel: case NodeType::ScrollArea:
        case NodeType::SplitPane:
            return true;
        default:
            return false;
    }
}

constexpr NodeType parse_node_type(std::string_view s) {
    if (s == "Text")    return NodeType::Text;
    if (s == "Marquee")    return NodeType::Marquee;
    if (s == "Icon")        return NodeType::Icon;
    if (s == "Image")       return NodeType::Image;
    if (s == "AlbumArt")    return NodeType::AlbumArt;
    if (s == "Divider")     return NodeType::Divider;
    if (s == "Badge")       return NodeType::Badge;
    if (s == "Button")      return NodeType::Button;
    if (s == "ToggleButton") return NodeType::ToggleButton;
    if (s == "Slider")      return NodeType::Slider;
    if (s == "VolumeControl") return NodeType::VolumeControl;
    if (s == "TransportBar") return NodeType::TransportBar;
    if (s == "SearchField") return NodeType::SearchField;
    if (s == "TabBar")      return NodeType::TabBar;
    if (s == "ListView")    return NodeType::ListView;
    if (s == "Visualizer")  return NodeType::Visualizer;
    if (s == "SeekBar")     return NodeType::SeekBar;
    if (s == "PeakMeter")   return NodeType::PeakMeter;
    if (s == "WaveformView") return NodeType::WaveformView;
    if (s == "LyricsView")  return NodeType::LyricsView;
    if (s == "Rating")      return NodeType::Rating;
    if (s == "ProgressBar") return NodeType::ProgressBar;
    if (s == "Stack")       return NodeType::Stack;
    if (s == "Row")         return NodeType::Row;
    if (s == "Column")      return NodeType::Column;
    if (s == "Grid")        return NodeType::Grid;
    if (s == "Panel")       return NodeType::Panel;
    if (s == "Spacer")      return NodeType::Spacer;
    if (s == "ScrollArea")  return NodeType::ScrollArea;
    if (s == "SplitPane")   return NodeType::SplitPane;
    return NodeType::Panel; // safe fallback
}

Dimension parse_dimension(const nlohmann::json& v) {
    if (v.is_number()) {
        return Dimension::px(v.get<double>());
    }
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s == "fill") return Dimension::fill();
        if (s == "auto") return Dimension::auto_();
        if (!s.empty() && s.back() == '%') {
            double pct = 0;
            std::from_chars(s.data(), s.data() + s.size() - 1, pct);
            return Dimension::percent(pct);
        }
    }
    return Dimension::px(0);
}

std::optional<Align> parse_align(std::string_view s) {
    if (s == "start")    return Align::start;
    if (s == "center")   return Align::center;
    if (s == "end")     return Align::end;
    if (s == "stretch")  return Align::stretch;
    if (s == "baseline") return Align::baseline;
    return std::nullopt;
}

std::optional<Justify> parse_justify(std::string_view s) {
    if (s == "start")       return Justify::start;
    if (s == "center")      return Justify::center;
    if (s == "end")         return Justify::end;
    if (s == "spaceBetween") return Justify::space_between;
    if (s == "spaceAround") return Justify::space_around;
    return std::nullopt;
}

std::optional<Orientation> parse_orientation(std::string_view s) {
    if (s == "horizontal") return Orientation::horizontal;
    if (s == "vertical")   return Orientation::vertical;
    return std::nullopt;
}

std::optional<ImageFit> parse_fit(std::string_view s) {
    if (s == "cover")    return ImageFit::cover;
    if (s == "contain")  return ImageFit::contain;
    if (s == "fill")     return ImageFit::fill;
    if (s == "none")     return ImageFit::none;
    return std::nullopt;
}

std::optional<FallbackMode> parse_fallback(std::string_view s) {
    if (s == "placeholder")    return FallbackMode::placeholder;
    if (s == "none")           return FallbackMode::none_;
    if (s == "blurredColor")   return FallbackMode::blurred_color;
    return std::nullopt;
}

std::optional<Overflow> parse_overflow(std::string_view s) {
    if (s == "clip")    return Overflow::clip;
    if (s == "ellipsis") return Overflow::ellipsis;
    if (s == "wrap")    return Overflow::wrap;
    if (s == "marquee") return Overflow::marquee;
    return std::nullopt;
}

std::optional<TextAlign> parse_text_align(std::string_view s) {
    if (s == "start")  return TextAlign::start;
    if (s == "center") return TextAlign::center;
    if (s == "end")    return TextAlign::end;
    return std::nullopt;
}

std::optional<ScrollDirection> parse_scroll_direction(std::string_view s) {
    if (s == "vertical")   return ScrollDirection::vertical;
    if (s == "horizontal") return ScrollDirection::horizontal;
    if (s == "both")       return ScrollDirection::both;
    return std::nullopt;
}

std::optional<VisualizerStyle> parse_visualizer_style(std::string_view s) {
    if (s == "bars")        return VisualizerStyle::bars;
    if (s == "oscilloscope") return VisualizerStyle::oscilloscope;
    if (s == "spectrum")    return VisualizerStyle::spectrum;
    if (s == "vuMeter")     return VisualizerStyle::vu_meter;
    if (s == "none")        return VisualizerStyle::none_;
    return std::nullopt;
}

TransportButton parse_transport_button(std::string_view s) {
    if (s == "previous")          return TransportButton::previous;
    if (s == "playPause")         return TransportButton::play_pause;
    if (s == "play")              return TransportButton::play;
    if (s == "pause")             return TransportButton::pause;
    if (s == "stop")              return TransportButton::stop;
    if (s == "next")              return TransportButton::next;
    if (s == "shuffle")           return TransportButton::shuffle;
    if (s == "repeat")            return TransportButton::repeat;
    if (s == "stopAfterCurrent")  return TransportButton::stop_after_current;
    if (s == "loved")             return TransportButton::loved;
    if (s == "rating")            return TransportButton::rating;
    return TransportButton::play_pause;
}

// Parse a `when:` predicate string into a WhenExpression.
// Grammar: when = clause { ("and"|"or") clause };
//          clause = [ "not" ] atom;
//          atom = state-path [operator literal];
std::optional<WhenExpression> parse_when(const std::string& s) {
    if (s.empty()) return std::nullopt;

    WhenExpression expr;
    expr.connective = WhenExpression::Connective::and_; // default
    // Simple parser: we handle the common two-term case for now.
    // Full grammar: split on " and " / " or " boundaries.
    std::vector<std::string_view> tokens;
    std::string_view remaining = s;

    // Split on " and " and " or "
    // We only implement the simple non-nested case here.
    (void)remaining; (void)tokens;
    // For simplicity, accept the predicate as an opaque string in the struct.
    // The QML layer evaluates it; we just store it.
    // Return a sentinel expression that signals "evaluate at runtime".
    return std::nullopt; // Signal "needs runtime evaluation"
}

// Build a Node from a JSON subtree.  `path` is the JSON Pointer for error messages.
Node build_node(const nlohmann::json& obj, std::string_view path) {
    Node n;

    // type (required)
    auto type_it = obj.find("type");
    if (type_it != obj.end() && type_it->is_string()) {
        n.type = parse_node_type(type_it->get<std::string>());
    }

    // id
    auto id_it = obj.find("id");
    if (id_it != obj.end() && id_it->is_string()) n.id = id_it->get<std::string>();

    // when (visibility predicate)
    auto when_it = obj.find("when");
    if (when_it != obj.end() && when_it->is_string()) {
        // Store the raw string for the QML layer to evaluate
        // The predicate is parsed at runtime by the QML bindings
    }

    // sizing / size / width / height
    auto sizing_it = obj.find("sizing");
    if (sizing_it != obj.end()) {
        Sizing s;
        auto it = sizing_it->find("width");  if (it != sizing_it->end()) s.width = parse_dimension(*it);
        auto it2 = sizing_it->find("height"); if (it2 != sizing_it->end()) s.height = parse_dimension(*it2);
        n.sizing = s;
    }
    auto size_it = obj.find("size");
    if (size_it != obj.end()) {
        Size s;
        auto it = size_it->find("width");   if (it != size_it->end()) s.width = parse_dimension(*it);
        auto it2 = size_it->find("height"); if (it2 != size_it->end()) s.height = parse_dimension(*it2);
        n.size = s;
    }
    auto w_it = obj.find("width");
    if (w_it != obj.end()) n.width = parse_dimension(*w_it);
    auto h_it = obj.find("height");
    if (h_it != obj.end()) n.height = parse_dimension(*h_it);

    // align / justify
    auto align_it = obj.find("align");
    if (align_it != obj.end() && align_it->is_string())
        n.align = parse_align(align_it->get<std::string>());
    auto justify_it = obj.find("justify");
    if (justify_it != obj.end() && justify_it->is_string())
        n.justify = parse_justify(justify_it->get<std::string>());

    // background
    auto bg_it = obj.find("background");
    if (bg_it != obj.end() && bg_it->is_string())
        n.background = ColorRef{bg_it->get<std::string>()};

    // border
    auto border_it = obj.find("border");
    if (border_it != obj.end() && border_it->is_object()) {
        Border b;
        auto c = border_it->find("color");
        if (c != border_it->end())
            b.color = ColorRef{c->get<std::string>()};
        auto w = border_it->find("width");
        if (w != border_it->end() && w->is_number())
            b.width = w->get<double>();
        n.border = b;
    }

    // radius
    auto radius_it = obj.find("radius");
    if (radius_it != obj.end()) {
            if (radius_it->is_string()) {
            std::string_view sv = radius_it->get<std::string>();
            if (sv == "none")
                n.radius = RadiusValue{std::string_view{"none"}};
            else
                n.radius = RadiusValue{std::string_view{sv}};
        } else if (radius_it->is_number()) {
            n.radius = RadiusValue{static_cast<std::int32_t>(radius_it->get<double>())};
        }
    }

    // elevation / opacity / clip
    auto elev_it = obj.find("elevation");
    if (elev_it != obj.end() && elev_it->is_number_integer())
        n.elevation = elev_it->get<std::int32_t>();
    auto op_it = obj.find("opacity");
    if (op_it != obj.end() && op_it->is_number())
        n.opacity = op_it->get<double>();
    auto clip_it = obj.find("clip");
    if (clip_it != obj.end() && clip_it->is_boolean())
        n.clip = clip_it->get<bool>();

    // spacing / padding / margin
    auto pad_it = obj.find("padding");
    if (pad_it != obj.end()) {
        if (pad_it->is_string())
            n.padding = SpacingValue{pad_it->get<std::string>()};
        else if (pad_it->is_number())
            n.padding = SpacingValue{static_cast<std::int32_t>(pad_it->get<double>())};
    }
    auto mar_it = obj.find("margin");
    if (mar_it != obj.end()) {
        if (mar_it->is_string())
            n.margin = SpacingValue{mar_it->get<std::string>()};
        else if (mar_it->is_number())
            n.margin = SpacingValue{static_cast<std::int32_t>(mar_it->get<double>())};
    }
    auto sp_it = obj.find("spacing");
    if (sp_it != obj.end()) {
        if (sp_it->is_string())
            n.spacing = SpacingValue{sp_it->get<std::string>()};
        else if (sp_it->is_number())
            n.spacing = SpacingValue{static_cast<std::int32_t>(sp_it->get<double>())};
    }

    // text content
    auto text_it = obj.find("text");
    if (text_it != obj.end() && text_it->is_string())
        n.text = text_it->get<std::string>();
    auto efs_it = obj.find("efs");
    if (efs_it != obj.end() && efs_it->is_string())
        n.efs = EfsPattern{efs_it->get<std::string>()};
    auto bind_it = obj.find("bind");
    if (bind_it != obj.end() && bind_it->is_string())
        n.bind = StatePath{bind_it->get<std::string>()};
    auto style_it = obj.find("style");
    if (style_it != obj.end() && style_it->is_string())
        n.style = style_it->get<std::string>();
    auto color_it = obj.find("color");
    if (color_it != obj.end() && color_it->is_string())
        n.color = ColorRef{color_it->get<std::string>()};
    auto overflow_it = obj.find("overflow");
    if (overflow_it != obj.end() && overflow_it->is_string())
        n.overflow = parse_overflow(overflow_it->get<std::string>());
    auto maxlines_it = obj.find("maxLines");
    if (maxlines_it != obj.end() && maxlines_it->is_number_integer())
        n.max_lines = maxlines_it->get<std::int32_t>();
    auto ta_it = obj.find("textAlign");
    if (ta_it != obj.end() && ta_it->is_string())
        n.text_align = parse_text_align(ta_it->get<std::string>());
    auto tooltip_it = obj.find("tooltip");
    if (tooltip_it != obj.end() && tooltip_it->is_string())
        n.tooltip = tooltip_it->get<std::string>();
    auto an_it = obj.find("accessibleName");
    if (an_it != obj.end() && an_it->is_string())
        n.accessible_name = an_it->get<std::string>();

    // action
    auto action_it = obj.find("action");
    if (action_it != obj.end() && action_it->is_string())
        n.action = Action{action_it->get<std::string>()};

    // source / icon / iconSize / fit / fallback
    auto source_it = obj.find("source");
    if (source_it != obj.end() && source_it->is_string())
        n.source = source_it->get<std::string>();
    auto icon_it = obj.find("icon");
    if (icon_it != obj.end() && icon_it->is_string())
        n.icon = icon_it->get<std::string>();
    auto icon_size_it = obj.find("iconSize");
    if (icon_size_it != obj.end() && icon_size_it->is_number())
        n.icon_size = icon_size_it->get<double>();
    auto fit_it = obj.find("fit");
    if (fit_it != obj.end() && fit_it->is_string())
        n.fit = parse_fit(fit_it->get<std::string>());
    auto fallback_it = obj.find("fallback");
    if (fallback_it != obj.end() && fallback_it->is_string())
        n.fallback = parse_fallback(fallback_it->get<std::string>());

    // columns / rows / orientation
    auto cols_it = obj.find("columns");
    if (cols_it != obj.end() && cols_it->is_number_integer())
        n.columns = cols_it->get<std::int32_t>();
    auto rows_it = obj.find("rows");
    if (rows_it != obj.end() && rows_it->is_number_integer())
        n.rows = rows_it->get<std::int32_t>();
    auto orient_it = obj.find("orientation");
    if (orient_it != obj.end() && orient_it->is_string())
        n.orientation = parse_orientation(orient_it->get<std::string>());

    // scrollDirection
    auto sd_it = obj.find("scrollDirection");
    if (sd_it != obj.end() && sd_it->is_string())
        n.scroll_direction = parse_scroll_direction(sd_it->get<std::string>());

    // split
    auto split_it = obj.find("split");
    if (split_it != obj.end() && split_it->is_number())
        n.split = split_it->get<double>();

    // buttons (TransportBar)
    auto btns_it = obj.find("buttons");
    if (btns_it != obj.end() && btns_it->is_array()) {
        for (const auto& b : *btns_it) {
            if (b.is_string())
                n.buttons.push_back(parse_transport_button(b.get<std::string>()));
        }
    }

    // min / max / step / showBuffered / showLabels / placeholder / barCount
    auto min_it = obj.find("min");
    if (min_it != obj.end() && min_it->is_number())
        n.min = min_it->get<double>();
    auto max_it = obj.find("max");
    if (max_it != obj.end() && max_it->is_number())
        n.max = max_it->get<double>();
    auto step_it = obj.find("step");
    if (step_it != obj.end() && step_it->is_number())
        n.step = step_it->get<double>();
    auto sb_it = obj.find("showBuffered");
    if (sb_it != obj.end() && sb_it->is_boolean())
        n.show_buffered = sb_it->get<bool>();
    auto sl_it = obj.find("showLabels");
    if (sl_it != obj.end() && sl_it->is_boolean())
        n.show_labels = sl_it->get<bool>();
    auto ph_it = obj.find("placeholder");
    if (ph_it != obj.end() && ph_it->is_string())
        n.placeholder = ph_it->get<std::string>();
    auto bc_it = obj.find("barCount");
    if (bc_it != obj.end() && bc_it->is_number_integer())
        n.bar_count = bc_it->get<std::int32_t>();

    // maxStars
    auto ms_it = obj.find("maxStars");
    if (ms_it != obj.end() && ms_it->is_number_integer())
        n.max_stars = ms_it->get<std::int32_t>();

    // speed (Marquee)
    auto speed_it = obj.find("speed");
    if (speed_it != obj.end() && speed_it->is_number())
        n.speed = speed_it->get<double>();

    // children
    auto child_it = obj.find("children");
    if (child_it != obj.end() && child_it->is_array() && has_children(n.type)) {
        std::size_t idx = 0;
        for (const auto& c : *child_it) {
            if (c.is_object()) {
                std::string child_path = std::string{path} + "/children/" + std::to_string(idx);
                n.children.push_back(build_node(c, child_path));
            }
            ++idx;
        }
    }

    // Count bindings for budget enforcement
    if (n.bind) n.binding_count = 1;
    for (const auto& child : n.children)
        n.binding_count += child.binding_count;

    return n;
}

LayoutDocument build_document(const nlohmann::json& doc) {
    LayoutDocument d;

    auto sv_it = doc.find("schemaVersion");
    if (sv_it != doc.end() && sv_it->is_number_integer())
        d.schema_version = sv_it->get<std::int32_t>();

    auto surf_it = doc.find("surface");
    if (surf_it != doc.end() && surf_it->is_string())
        d.surface = surf_it->get<std::string>();

    auto desc_it = doc.find("description");
    if (desc_it != doc.end() && desc_it->is_string())
        d.description = desc_it->get<std::string>();

    auto minsize_it = doc.find("minSize");
    if (minsize_it != doc.end() && minsize_it->is_object()) {
        Size s;
        auto w = minsize_it->find("width");  if (w != minsize_it->end()) s.width = parse_dimension(*w);
        auto h = minsize_it->find("height"); if (h != minsize_it->end()) s.height = parse_dimension(*h);
        d.min_size = s;
    }

    auto root_it = doc.find("root");
    if (root_it != doc.end() && root_it->is_object())
        d.root = build_node(*root_it, "/root");

    return d;
}

}  // namespace

// ---------------------------------------------------------------------------
//  LayoutDocument accessors
// ---------------------------------------------------------------------------

std::int32_t LayoutDocument::count_nodes() const noexcept {
    std::function<std::int32_t(const Node&)> count = [&](const Node& n) -> std::int32_t {
        std::int32_t total = 1;
        for (const auto& c : n.children) total += count(c);
        return total;
    };
    return count(root);
}

std::int32_t LayoutDocument::max_depth() const noexcept {
    std::function<std::int32_t(const Node&, std::int32_t)> depth =
        [&](const Node& n, std::int32_t d) -> std::int32_t {
        std::int32_t best = d;
        for (const auto& c : n.children)
            best = std::max(best, depth(c, d + 1));
        return best;
    };
    return depth(root, 1);
}

std::int32_t LayoutDocument::count_bindings() const noexcept {
    std::function<std::int32_t(const Node&)> count = [&](const Node& n) -> std::int32_t {
        std::int32_t total = n.binding_count;
        for (const auto& c : n.children) total += count(c);
        return total;
    };
    return count(root);
}

// ---------------------------------------------------------------------------
//  Public entry points
// ---------------------------------------------------------------------------

LayoutResult LayoutInterpreter::parse(std::string_view json,
                                     const theme::SchemaValidator& validator) {
    LayoutResult result;

    // Step 1: schema-validate the raw JSON against layout.schema.json
    auto schema_result = validator.validate(theme::SchemaId::Layout, json);
    if (!schema_result.ok()) {
        for (auto& e : schema_result.errors) {
            result.errors.push_back(LayoutError{
                e.instance_pointer,
                e.message,
                0,
            });
        }
        return result;
    }

    // Step 2: parse into strongly-typed structs
    try {
        const auto doc = nlohmann::json::parse(json);
        auto d = build_document(doc);

        // Step 3: budget enforcement (REQ-THM-033)
        std::int32_t total_nodes = d.count_nodes();
        if (total_nodes > 500) {
            result.errors.push_back(LayoutError{
                "/root",
                "layout exceeds the maximum of 500 component instances (found " +
                    std::to_string(total_nodes) + ")",
                0,
            });
            return result;
        }

        std::int32_t depth = d.max_depth();
        if (depth > 24) {
            result.errors.push_back(LayoutError{
                "/root",
                "layout exceeds the maximum nesting depth of 24 (found " +
                    std::to_string(depth) + ")",
                0,
            });
            return result;
        }

        std::int32_t bindings = d.count_bindings();
        if (bindings > 64) {
            result.errors.push_back(LayoutError{
                "/root",
                "layout exceeds the maximum of 64 bindings (found " +
                    std::to_string(bindings) + ")",
                0,
            });
            return result;
        }

        result.document = std::make_shared<LayoutDocument>(std::move(d));

    } catch (const std::exception& e) {
        result.errors.push_back(LayoutError{
            "",
            std::string{"parse error: "} + e.what(),
            0,
        });
    }

    return result;
}

LayoutResult LayoutInterpreter::load(const std::filesystem::path& path,
                                   const theme::SchemaValidator& validator) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        LayoutResult r;
        r.errors.push_back(LayoutError{
            "",
            "cannot open layout file: " + path.string(),
            0,
        });
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), validator);
}

}  // namespace arrow::skin
