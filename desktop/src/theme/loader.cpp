// SPDX-License-Identifier: MPL-2.0
//
// loader.cpp — see loader.hpp for design notes.
//
// This file turns a validated nlohmann::json into a Theme. Every field
// has to be read defensively: the schema says the document is well-typed,
// but the loader is the boundary that converts from a loosely-typed JSON
// tree into a strongly-typed struct, and a defensive read here means a
// strict invariant everywhere downstream.

#include "theme/loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace arrow::theme {

namespace {

// ---------------------------------------------------------------------------
//  Small helpers — none of these do anything except pull a value out of
//  a JSON node with a precise failure shape. They are NOT a re-implementation
//  of the schema: validation has already happened by the time we get here.
// ---------------------------------------------------------------------------

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error{msg};
}

const nlohmann::json& require(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        fail(std::string{"missing required key '"} + key + "'");
    }
    return *it;
}

std::string require_string(const nlohmann::json& obj, const char* key) {
    const auto& v = require(obj, key);
    if (!v.is_string()) fail(std::string{key} + " is not a string");
    return v.get<std::string>();
}

std::optional<std::string> opt_string(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_string()) fail(std::string{key} + " is not a string");
    return it->get<std::string>();
}

double require_number(const nlohmann::json& obj, const char* key) {
    const auto& v = require(obj, key);
    if (!v.is_number()) fail(std::string{key} + " is not a number");
    return v.get<double>();
}

std::optional<double> opt_number(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_number()) fail(std::string{key} + " is not a number");
    return it->get<double>();
}

int require_int(const nlohmann::json& obj, const char* key) {
    const auto& v = require(obj, key);
    if (!v.is_number_integer()) fail(std::string{key} + " is not an integer");
    return v.get<int>();
}

std::optional<int> opt_int(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_number_integer()) fail(std::string{key} + " is not an integer");
    return it->get<int>();
}

bool require_bool(const nlohmann::json& obj, const char* key) {
    const auto& v = require(obj, key);
    if (!v.is_boolean()) fail(std::string{key} + " is not a boolean");
    return v.get<bool>();
}

std::optional<bool> opt_bool(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_boolean()) fail(std::string{key} + " is not a boolean");
    return it->get<bool>();
}

Color require_color(const nlohmann::json& obj, const char* key) {
    return Color{require_string(obj, key)};
}

std::optional<Color> opt_color(const nlohmann::json& obj, const char* key) {
    auto s = opt_string(obj, key);
    if (!s) return std::nullopt;
    return Color{std::move(*s)};
}

std::vector<FontFamily> require_font_stack(const nlohmann::json& obj, const char* key) {
    const auto& v = require(obj, key);
    if (!v.is_array()) fail(std::string{key} + " is not an array");
    std::vector<FontFamily> out;
    out.reserve(v.size());
    for (const auto& item : v) {
        if (!item.is_string()) fail(std::string{key} + " entries must be strings");
        out.push_back(FontFamily{item.get<std::string>()});
    }
    if (out.empty()) fail(std::string{key} + " must have at least one entry");
    return out;
}

std::optional<std::vector<FontFamily>> opt_font_stack(const nlohmann::json& obj,
                                                      const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    if (!it->is_array()) fail(std::string{key} + " is not an array");
    std::vector<FontFamily> out;
    out.reserve(it->size());
    for (const auto& item : *it) {
        if (!item.is_string()) fail(std::string{key} + " entries must be strings");
        out.push_back(FontFamily{item.get<std::string>()});
    }
    if (out.empty()) fail(std::string{key} + " must have at least one entry");
    return out;
}

TypeStyle require_type_style(const nlohmann::json& obj) {
    TypeStyle out;
    out.size = require_number(obj, "size");
    out.line_height    = opt_number(obj, "lineHeight");
    auto w             = opt_int(obj, "weight");
    if (w) out.weight  = *w;
    out.letter_spacing = opt_number(obj, "letterSpacing");
    out.transform      = opt_string(obj, "transform");
    return out;
}

std::optional<TypeStyle> opt_type_style(const nlohmann::json& obj) {
    auto it = obj.find("size");
    if (it == obj.end() || it->is_null()) return std::nullopt;
    return require_type_style(obj);
}

CubicBezier require_cubic(const nlohmann::json& v) {
    if (!v.is_array() || v.size() != 4)
        fail("cubic-bezier must be an array of 4 numbers");
    CubicBezier out;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!v[i].is_number()) fail("cubic-bezier entry is not a number");
        switch (i) {
            case 0: out.x1 = v[i].get<double>(); break;
            case 1: out.y1 = v[i].get<double>(); break;
            case 2: out.x2 = v[i].get<double>(); break;
            case 3: out.y2 = v[i].get<double>(); break;
        }
    }
    return out;
}

std::optional<CubicBezier> opt_cubic(const nlohmann::json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::nullopt;
    return require_cubic(*it);
}

// ---------------------------------------------------------------------------
//  Token-group readers
// ---------------------------------------------------------------------------

BackgroundTokens read_background(const nlohmann::json& obj) {
    BackgroundTokens out;
    out.base     = require_color(obj, "base");
    out.sunken   = opt_color(obj, "sunken");
    out.raised   = opt_color(obj, "raised");
    out.overlay  = opt_color(obj, "overlay");
    out.scrim    = opt_color(obj, "scrim");
    return out;
}

SurfaceTokens read_surface(const nlohmann::json& obj) {
    SurfaceTokens out;
    out.base     = require_color(obj, "base");
    out.hover    = opt_color(obj, "hover");
    out.pressed  = opt_color(obj, "pressed");
    out.selected = opt_color(obj, "selected");
    out.disabled = opt_color(obj, "disabled");
    return out;
}

TextTokens read_text(const nlohmann::json& obj) {
    TextTokens out;
    out.primary   = require_color(obj, "primary");
    out.secondary = require_color(obj, "secondary");
    out.tertiary  = opt_color(obj, "tertiary");
    out.disabled  = opt_color(obj, "disabled");
    out.inverse   = opt_color(obj, "inverse");
    out.on_accent = opt_color(obj, "onAccent");
    out.link      = opt_color(obj, "link");
    return out;
}

AccentTokens read_accent(const nlohmann::json& obj) {
    AccentTokens out;
    out.base    = require_color(obj, "base");
    out.hover   = opt_color(obj, "hover");
    out.pressed = opt_color(obj, "pressed");
    out.subtle  = opt_color(obj, "subtle");
    out.muted   = opt_color(obj, "muted");
    return out;
}

BorderTokens read_border(const nlohmann::json& obj) {
    BorderTokens out;
    out.base   = require_color(obj, "base");
    out.subtle = opt_color(obj, "subtle");
    out.strong = opt_color(obj, "strong");
    out.focus  = opt_color(obj, "focus");
    return out;
}

StateTokens read_state(const nlohmann::json& obj) {
    StateTokens out;
    out.success = opt_color(obj, "success");
    out.warning = opt_color(obj, "warning");
    out.error_  = opt_color(obj, "error");
    out.info    = opt_color(obj, "info");
    return out;
}

PlaybackTokens read_playback(const nlohmann::json& obj) {
    PlaybackTokens out;
    out.progress          = opt_color(obj, "progress");
    out.progress_track     = opt_color(obj, "progressTrack");
    out.buffered           = opt_color(obj, "buffered");
    out.waveform           = opt_color(obj, "waveform");
    out.waveform_played    = opt_color(obj, "waveformPlayed");
    out.peak_meter         = opt_color(obj, "peakMeter");
    out.peak_meter_clip    = opt_color(obj, "peakMeterClip");
    return out;
}

VisualizerTokens read_visualizer(const nlohmann::json& obj) {
    VisualizerTokens out;
    if (auto it = obj.find("palette"); it != obj.end() && !it->is_null()) {
        if (!it->is_array()) fail("visualizer.palette is not an array");
        std::vector<Color> pal;
        pal.reserve(it->size());
        for (const auto& c : *it) {
            if (!c.is_string()) fail("visualizer.palette entry is not a string");
            pal.push_back(Color{c.get<std::string>()});
        }
        if (pal.empty()) fail("visualizer.palette must have at least one entry");
        out.palette = std::move(pal);
    }
    out.background = opt_color(obj, "background");
    return out;
}

ColorTokens read_color(const nlohmann::json& obj) {
    ColorTokens out;
    out.background = read_background(require(obj, "background"));
    out.surface    = read_surface(require(obj, "surface"));
    out.text       = read_text(require(obj, "text"));
    out.accent     = read_accent(require(obj, "accent"));
    out.border     = read_border(require(obj, "border"));
    if (auto it = obj.find("state");      it != obj.end()) out.state      = read_state(*it);
    if (auto it = obj.find("playback");   it != obj.end()) out.playback   = read_playback(*it);
    if (auto it = obj.find("visualizer"); it != obj.end()) out.visualizer = read_visualizer(*it);
    return out;
}

FontStacks read_font_family(const nlohmann::json& obj) {
    FontStacks out;
    out.sans    = require_font_stack(obj, "sans");
    out.mono    = opt_font_stack(obj, "mono").value_or(out.mono);
    out.display = opt_font_stack(obj, "display").value_or(out.display);
    return out;
}

TypeScale read_type_scale(const nlohmann::json& obj) {
    TypeScale out;
    out.display  = opt_type_style(obj);
    out.headline = opt_type_style(obj);
    out.title    = opt_type_style(obj);
    out.body     = require_type_style(require(obj, "body"));
    out.label    = require_type_style(require(obj, "label"));
    out.caption  = opt_type_style(obj);
    out.mono     = opt_type_style(obj);
    return out;
}

TypographyTokens read_typography(const nlohmann::json& obj) {
    TypographyTokens out;
    out.font_family = read_font_family(require(obj, "fontFamily"));
    out.base_size   = opt_number(obj, "baseSize");
    out.scale       = read_type_scale(require(obj, "scale"));
    return out;
}

ShapeTokens read_shape(const nlohmann::json& obj) {
    ShapeTokens out;
    if (auto it = obj.find("radius"); it != obj.end()) {
        ShapeRadius r;
        if (auto jt = it->find("none"); jt != it->end() && !jt->is_null()) r.none  = require_int(*it, "none");
        if (auto jt = it->find("sm");   jt != it->end() && !jt->is_null()) r.sm    = require_int(*it, "sm");
        if (auto jt = it->find("md");   jt != it->end() && !jt->is_null()) r.md    = require_int(*it, "md");
        if (auto jt = it->find("lg");   jt != it->end() && !jt->is_null()) r.lg    = require_int(*it, "lg");
        if (auto jt = it->find("xl");   jt != it->end() && !jt->is_null()) r.xl    = require_int(*it, "xl");
        if (auto jt = it->find("full"); jt != it->end() && !jt->is_null()) r.full  = require_int(*it, "full");
        out.radius = r;
    }
    if (auto it = obj.find("borderWidth"); it != obj.end()) {
        ShapeBorderWidth b;
        b.hairline = opt_number(*it, "hairline");
        b.thin     = opt_number(*it, "thin");
        b.thick    = opt_number(*it, "thick");
        out.border_width = b;
    }
    return out;
}

SpacingTokens read_spacing(const nlohmann::json& obj) {
    SpacingTokens out;
    out.unit    = opt_int(obj, "unit");
    out.density = opt_string(obj, "density");
    if (auto it = obj.find("scale"); it != obj.end() && !it->is_null()) {
        if (!it->is_array()) fail("spacing.scale is not an array");
        std::vector<int> s;
        s.reserve(it->size());
        for (const auto& v : *it) {
            if (!v.is_number_integer()) fail("spacing.scale entry is not an integer");
            s.push_back(v.get<int>());
        }
        if (s.size() < 4) fail("spacing.scale must have at least 4 entries");
        out.scale = std::move(s);
    }
    return out;
}

std::vector<Elevation> read_elevation(const nlohmann::json& v) {
    if (!v.is_array()) fail("elevation must be an array");
    if (v.size() > 6) fail("elevation must have at most 6 entries");
    std::vector<Elevation> out;
    out.reserve(v.size());
    for (const auto& item : v) {
        Elevation e;
        e.offset_x = opt_number(item, "offsetX").value_or(0.0);
        e.offset_y = require_number(item, "offsetY");
        e.blur     = require_number(item, "blur");
        e.spread   = opt_number(item, "spread").value_or(0.0);
        e.color    = require_color(item, "color");
        out.push_back(e);
    }
    return out;
}

MotionTokens read_motion(const nlohmann::json& obj) {
    MotionTokens out;
    if (auto it = obj.find("duration"); it != obj.end()) {
        MotionDuration d;
        d.instant = opt_int(*it, "instant");
        d.fast    = opt_int(*it, "fast");
        d.normal  = opt_int(*it, "normal");
        d.slow    = opt_int(*it, "slow");
        out.duration = d;
    }
    if (auto it = obj.find("easing"); it != obj.end()) {
        MotionEasing e;
        e.standard    = opt_cubic(*it, "standard");
        e.decelerate  = opt_cubic(*it, "decelerate");
        e.accelerate  = opt_cubic(*it, "accelerate");
        e.emphasized  = opt_cubic(*it, "emphasized");
        out.easing = e;
    }
    return out;
}

OpacityTokens read_opacity(const nlohmann::json& obj) {
    OpacityTokens out;
    out.disabled = opt_number(obj, "disabled");
    out.hover    = opt_number(obj, "hover");
    out.pressed  = opt_number(obj, "pressed");
    out.scrim    = opt_number(obj, "scrim");
    out.ghost    = opt_number(obj, "ghost");
    return out;
}

IconTokens read_icons(const nlohmann::json& obj) {
    IconTokens out;
    out.set_id       = opt_string(obj, "setId");
    out.style        = opt_string(obj, "style");
    out.stroke_width = opt_number(obj, "strokeWidth");
    out.size_scale   = opt_number(obj, "sizeScale");
    return out;
}

AssetTokens read_assets(const nlohmann::json& obj) {
    AssetTokens out;
    out.background        = opt_string(obj, "background");
    out.background_fit    = opt_string(obj, "backgroundFit");
    out.background_opacity = opt_number(obj, "backgroundOpacity");
    out.logo              = opt_string(obj, "logo");
    return out;
}

AccessibilityTokens read_a11y(const nlohmann::json& obj) {
    AccessibilityTokens out;
    out.contrast_target         = opt_string(obj, "contrastTarget");
    out.respects_reduced_motion = opt_bool(obj, "respectsReducedMotion");
    out.min_touch_target        = opt_int(obj, "minTouchTarget");
    out.focus_ring_width        = opt_int(obj, "focusRingWidth");
    return out;
}

// Build a Theme from a parsed JSON document. Throws std::runtime_error if
// the document is malformed in a way the schema validator did not catch —
// this is the loader's own invariant check, not a re-run of validation.
std::shared_ptr<Theme> build(const nlohmann::json& doc) {
    auto t = std::make_shared<Theme>();

    t->schema_version = require_int(doc, "schemaVersion");
    t->id             = require_string(doc, "id");
    t->name           = require_string(doc, "name");
    t->author         = opt_string(doc, "author");
    t->version        = require_string(doc, "version");
    t->license        = opt_string(doc, "license");
    t->homepage       = opt_string(doc, "homepage");
    t->description    = opt_string(doc, "description");
    t->min_app_version = opt_string(doc, "minAppVersion");
    t->extends        = opt_string(doc, "extends");
    t->mode           = require_string(doc, "mode");
    if (t->mode != "light" && t->mode != "dark") {
        fail("mode must be \"light\" or \"dark\"");
    }
    t->color      = read_color(require(doc, "color"));
    t->typography = read_typography(require(doc, "typography"));

    if (auto it = doc.find("shape");      it != doc.end()) t->shape      = read_shape(*it);
    if (auto it = doc.find("spacing");    it != doc.end()) t->spacing    = read_spacing(*it);
    if (auto it = doc.find("elevation");  it != doc.end()) t->elevation  = read_elevation(*it);
    if (auto it = doc.find("motion");     it != doc.end()) t->motion     = read_motion(*it);
    if (auto it = doc.find("opacity");    it != doc.end()) t->opacity    = read_opacity(*it);
    if (auto it = doc.find("icons");      it != doc.end()) t->icons      = read_icons(*it);
    if (auto it = doc.find("assets");     it != doc.end()) t->assets     = read_assets(*it);
    if (auto it = doc.find("a11y");       it != doc.end()) t->a11y       = read_a11y(*it);

    return t;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Public entry points
// ---------------------------------------------------------------------------

ThemeLoader::LoadResult ThemeLoader::parse(std::string_view document,
                                           const SchemaValidator& validator) {
    LoadResult out;
    out.schema_errors = validator.validate(SchemaId::Theme, document).errors;
    if (!out.schema_errors.empty()) {
        return out;  // no Theme on a validation failure
    }
    try {
        nlohmann::json doc = nlohmann::json::parse(document);
        out.theme = build(doc);
    } catch (const std::exception& e) {
        out.theme.reset();
        SchemaError err;
        err.instance_pointer = "";
        err.message = std::string{"loader failed: "} + e.what();
        out.schema_errors.push_back(std::move(err));
    }
    return out;
}

ThemeLoader::LoadResult ThemeLoader::load(const std::filesystem::path& path,
                                          const SchemaValidator& validator) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LoadResult out;
        SchemaError err;
        err.message = "cannot open theme file: " + path.string();
        out.schema_errors.push_back(std::move(err));
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), validator);
}

}  // namespace arrow::theme
