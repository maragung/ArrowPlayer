// SPDX-License-Identifier: MPL-2.0
//
// tokens.cpp — see tokens.hpp for design notes.

#include "theme/tokens.hpp"

#include <fstream>
#include <limits>
#include <sstream>

#include <nlohmann/json.hpp>

namespace arrow::theme {

namespace {

// ---------------------------------------------------------------------------
//  Read helpers
// ---------------------------------------------------------------------------

/// Read a required numeric field from a JSON object, or add an error.
std::optional<double> get_number(const nlohmann::json& obj,
                                  const char* key,
                                  std::vector<std::string>* errors,
                                  const char* ctx) {
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        errors->push_back(std::string{ctx} + ": missing required field '" + key + "'");
        return std::nullopt;
    }
    if (!it->is_number()) {
        errors->push_back(std::string{ctx} + ": field '" + std::string{key} +
                          "' must be a number");
        return std::nullopt;
    }
    return it->get<double>();
}

std::optional<std::int32_t> get_int(const nlohmann::json& obj,
                                     const char* key,
                                     std::vector<std::string>* errors,
                                     const char* ctx) {
    auto v = get_number(obj, key, errors, ctx);
    if (!v) return std::nullopt;
    if (v < std::numeric_limits<std::int32_t>::min() ||
        v > std::numeric_limits<std::int32_t>::max()) {
        errors->push_back(std::string{ctx} + ": field '" + std::string{key} +
                          "' value is out of int32 range");
        return std::nullopt;
    }
    return static_cast<std::int32_t>(*v);
}

std::optional<std::int32_t> get_spacing(const nlohmann::json& obj,
                                         const char* key,
                                         std::vector<std::string>* errors,
                                         const char* ctx) {
    return get_int(obj, key, errors, ctx);
}

}  // namespace

// ---------------------------------------------------------------------------
//  TokenBundle::load
// ---------------------------------------------------------------------------

bool TokenBundle::load(std::string_view json, std::vector<std::string>* errors) noexcept {
    if (!errors) return false;

    try {
        const auto doc = nlohmann::json::parse(json);
        return load(doc, errors);
    } catch (const std::exception& e) {
        errors->push_back(std::string{"tokens.json parse error: "} + e.what());
        return false;
    }
}

bool TokenBundle::load(const std::filesystem::path& path,
                        std::vector<std::string>* errors) noexcept {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        if (errors) {
            errors->push_back("cannot open tokens.json: " + path.string());
        }
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return load(ss.str(), errors);
}

bool TokenBundle::load(const nlohmann::json& doc, std::vector<std::string>* errors) noexcept {
    if (!errors) errors = nullptr;

    // ── spacing ────────────────────────────────────────────────────────────
    if (auto it = doc.find("spacing"); it != doc.end() && it->is_object()) {
        const auto& sp = *it;
        const char* ctx = "spacing";
        if (auto v = get_spacing(sp, "xs",  errors, ctx)) spacing_.xs   = *v;
        if (auto v = get_spacing(sp, "sm",  errors, ctx)) spacing_.sm   = *v;
        if (auto v = get_spacing(sp, "md",  errors, ctx)) spacing_.md   = *v;
        if (auto v = get_spacing(sp, "lg",  errors, ctx)) spacing_.lg   = *v;
        if (auto v = get_spacing(sp, "xl",  errors, ctx)) spacing_.xl   = *v;
        if (auto v = get_spacing(sp, "2xl", errors, ctx)) spacing_.xxl  = *v;
        if (auto v = get_spacing(sp, "3xl", errors, ctx)) spacing_.xxxl = *v;
        if (auto v = get_spacing(sp, "4xl", errors, ctx)) spacing_.xxxxl = *v;

        // spacing._basis records the base unit; read it from a comment or use 4.
        if (auto it2 = sp.find("_basis"); it2 != sp.end()) {
            // The _basis field is a human-readable string like "4 px base unit".
            // We do not parse it; the default spacing_unit_ is always 4.
        }
    }

    // ── typography ─────────────────────────────────────────────────────────
    if (auto it = doc.find("typography"); it != doc.end() && it->is_object()) {
        const auto& tp = *it;

        if (auto v = get_number(tp, "baseSize", errors, "typography")) {
            (void)v;  // baseSize is stored on TypographyTokens, not TokenBundle
        }

        if (auto it2 = tp.find("scale"); it2 != tp.end() && it2->is_object()) {
            const auto& sc = *it2;

            auto read_type = [&](const char* key, TypeToken& out) {
                if (errors) {
                    std::string ctx = std::string{"typography.scale."} + key;
                    auto it3 = sc.find(key);
                    if (it3 == sc.end() || !it3->is_object()) {
                        // Type scale may be missing some entries; the canonical
                        // values are baked into the struct as defaults.
                        return;
                    }
                    const auto& obj = *it3;
                    if (auto v = get_number(obj, "size", errors, ctx.c_str()))
                        out.size = *v;
                    if (auto v = get_number(obj, "lineHeight", errors, ctx.c_str()))
                        out.line_height = *v;
                    if (auto v = get_int(obj, "weight", errors, ctx.c_str()))
                        out.weight = *v;
                    if (auto v = get_number(obj, "letterSpacing", errors, ctx.c_str()))
                        out.letter_spacing = *v;
                }
            };

            read_type("display",  typography_.display);
            read_type("headline", typography_.headline);
            read_type("title",    typography_.title);
            read_type("body",     typography_.body);
            read_type("label",    typography_.label);
            read_type("caption",  typography_.caption);
            read_type("mono",     typography_.mono);
        }
    }

    // ── radius ─────────────────────────────────────────────────────────────
    if (auto it = doc.find("radius"); it != doc.end() && it->is_object()) {
        const auto& rb = *it;
        const char* ctx = "radius";
        if (auto v = get_int(rb, "sm",   errors, ctx)) radius_.sm  = *v;
        if (auto v = get_int(rb, "md",   errors, ctx)) radius_.md  = *v;
        if (auto v = get_int(rb, "lg",   errors, ctx)) radius_.lg  = *v;
        if (auto v = get_int(rb, "xl",   errors, ctx)) radius_.xl  = *v;
        // "full" is a const 9999 — always correct, no override needed.
    }

    // ── elevation ───────────────────────────────────────────────────────────
    if (auto it = doc.find("elevation"); it != doc.end() && it->is_array()) {
        const auto& el = *it;
        const std::size_t max_levels = elevation_.levels.size();
        for (std::size_t i = 0; i < std::min(el.size(), max_levels); ++i) {
            if (!el[i].is_object()) continue;
            const auto& obj = el[i];
            if (auto v = get_int(obj, "offsetY", errors, "elevation"))
                elevation_.levels[i].offset_y = *v;
            if (auto v = get_int(obj, "blur", errors, "elevation"))
                elevation_.levels[i].blur = *v;
            if (auto v = get_number(obj, "alpha", errors, "elevation"))
                elevation_.levels[i].alpha = *v;
        }
    }

    // ── motion.duration ─────────────────────────────────────────────────────
    if (auto it = doc.find("motion"); it != doc.end() && it->is_object()) {
        if (auto it2 = it->find("duration"); it2 != it->end() && it2->is_object()) {
            const auto& d = *it2;
            const char* ctx = "motion.duration";
            if (auto v = get_int(d, "instant", errors, ctx)) motion_.instant.ms = *v;
            if (auto v = get_int(d, "fast",    errors, ctx)) motion_.fast.ms    = *v;
            if (auto v = get_int(d, "normal",  errors, ctx)) motion_.normal.ms  = *v;
            if (auto v = get_int(d, "slow",    errors, ctx)) motion_.slow.ms    = *v;
        }

        if (auto it2 = it->find("easing"); it2 != it->end() && it2->is_object()) {
            const auto& e = *it2;

            auto read_cubic = [&](const char* key, EasingToken& out) {
                if (errors) {
                    std::string ctx = std::string{"motion.easing."} + key;
                    auto it3 = e.find(key);
                    if (it3 == e.end() || !it3->is_object()) return;
                    const auto& obj = *it3;
                    auto it4 = obj.find("cubicBezier");
                    if (it4 == obj.end() || !it4->is_array() || it4->size() != 4) {
                        errors->push_back(ctx + ": cubicBezier must be an array of 4 numbers");
                        return;
                    }
                    out.x1 = (*it4)[0].get<double>();
                    out.y1 = (*it4)[1].get<double>();
                    out.x2 = (*it4)[2].get<double>();
                    out.y2 = (*it4)[3].get<double>();
                }
            };

            read_cubic("standard",    easing_.standard);
            read_cubic("decelerate",  easing_.decelerate);
            read_cubic("accelerate",  easing_.accelerate);
            read_cubic("emphasized", easing_.emphasized);
        }
    }

    // ── done ───────────────────────────────────────────────────────────────
    if (errors && !errors->empty()) return false;
    return true;
}

}  // namespace arrow::theme
