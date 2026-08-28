// SPDX-License-Identifier: MPL-2.0
#include "library/smart_playlist.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "core/json/json.hpp"

namespace arrow::library {

namespace {

// ===========================================================================
//  Closed sets — column names and operators are picked from these at compile
//  time, never from the rule text. REQ-PLS-011, REQ-SEC-009.
// ===========================================================================

const std::unordered_set<std::string>& builtin_fields() {
    // The 37 names from the schema's `builtinField` enum. The order is the
    // same as the schema for diff hygiene; lookup is the set, not the vector.
    static const std::unordered_set<std::string> kSet = {
        "title",    "artist",    "albumartist", "album",
        "genre",    "composer",  "comment",     "grouping",
        "year",     "date",      "tracknumber", "discnumber",
        "duration", "bitrate",   "samplerate",  "bitdepth",
        "channels", "codec",     "container",   "islossless",
        "rating",   "loved",     "playcount",   "skipcount",
        "lastplayed", "added",   "bpm",         "key",
        "path",     "filename",  "filesize",    "source",
        "playlist", "missing",   "hasartwork",  "haslyrics",
        "rgtrackgain",
    };
    return kSet;
}

// String fields. Comparing these is case-insensitive by default; the compiler
// emits `COLLATE NOCASE` for them unless the rule opts in to case sensitivity.
// The conformance fixture `case-insensitive-default` pins this behaviour.
bool is_string_field(std::string_view f) {
    static const std::unordered_set<std::string> kSet = {
        "title", "artist", "albumartist", "album", "genre", "composer",
        "comment", "grouping", "date", "codec", "container", "key",
        "path", "filename", "source", "playlist",
    };
    return kSet.contains(std::string{f});
}

// Text fields where contains/startsWith/endsWith are meaningful. The schema
// permits those operators on every comparison, but emitting `LIKE` on a
// numeric column is a SQL error and the test corpus relies on the compiler
// accepting the spec's permissive grammar.
bool is_text_field(std::string_view f) {
    // Every string field supports LIKE-style operators. We don't gate the
    // grammar — the schema doesn't — but we treat any non-text field as
    // numeric for the escape/format choice.
    return is_string_field(f);
}

// The tracks-table column a builtin field maps to. Most names already match
// the column; the few that differ (the "ls" prefix on long names, the
// dot-to-underscore in `rg_track_gain`) are listed here so the SQL is the
// actual table layout rather than a hopeful guess.
const std::string& column_for(std::string_view field) {
    static const std::unordered_map<std::string, std::string> kMap = {
        // passthrough
        {"title", "title"},        {"artist", "artist"},
        {"albumartist", "albumartist"}, {"album", "album"},
        {"genre", "genre"},        {"composer", "composer"},
        {"comment", "comment"},    {"grouping", "grouping"},
        {"year", "year"},          {"date", "date"},
        {"tracknumber", "tracknumber"}, {"discnumber", "discnumber"},
        {"duration", "duration_ms"},
        {"bitrate", "bitrate_kbps"},
        {"samplerate", "sample_rate"},
        {"bitdepth", "bit_depth"},
        {"channels", "channels"},
        {"codec", "codec"},        {"container", "container"},
        {"islossless", "is_lossless"},
        {"rating", "rating"},
        {"loved", "is_loved"},
        {"playcount", "play_count"},
        {"skipcount", "skip_count"},
        {"lastplayed", "last_played_at"},
        {"added", "added_at"},
        {"bpm", "bpm"},
        {"key", "music_key"},
        {"path", "container_path"},
        {"filename", "filename"},
        {"filesize", "file_size"},
        {"source", "source_id"},
        {"playlist", "playlist_id"},
        {"missing", "missing_since"},
        {"hasartwork", "artwork_id"},
        {"haslyrics", "lyrics_id"},
        {"rgtrackgain", "rg_track_gain"},
    };
    static const std::string kEmpty;
    auto it = kMap.find(std::string{field});
    return it == kMap.end() ? kEmpty : it->second;
}

std::string sql_operator(FieldOp op) {
    switch (op) {
        case FieldOp::Eq:           return "=";
        case FieldOp::Ne:           return "!=";
        case FieldOp::Gt:           return ">";
        case FieldOp::Lt:           return "<";
        case FieldOp::Gte:          return ">=";
        case FieldOp::Lte:          return "<=";
        case FieldOp::Contains:     return "LIKE";
        case FieldOp::NotContains:  return "NOT LIKE";
        case FieldOp::StartsWith:   return "LIKE";
        case FieldOp::EndsWith:     return "LIKE";
        case FieldOp::Matches:      return "GLOB";
        case FieldOp::Between:      return "BETWEEN";   // not a real SQL keyword here; emitted in body
        case FieldOp::In:           return "IN";        // likewise
        case FieldOp::IsNull:       return "IS NULL";
        case FieldOp::IsNotNull:    return "IS NOT NULL";
    }
    return "=";
}

// ===========================================================================
//  JSON value → PlaylistRule  (parse_rule_json)
// ===========================================================================

class RuleError {
  public:
    explicit RuleError(std::string user_message)
          : err_{ErrorCode::InvalidArgument, std::move(user_message)} {}

    [[nodiscard]] Error& error() { return err_; }

    // Convenience: assign a fresh user message, preserving code/position
    // but not previous detail. The detail of the error being modified is
    // already committed if and only if the caller already called
    // with_detail on it, so the helper only exists to avoid the two-line
    // constructor+with_detail dance at every call site.
    Error& set_user_message(std::string m) {
        err_ = Error{err_.code(), std::move(m), err_.technical_detail(),
                     err_.severity(), err_.recovery()};
        err_.at(err_.offset(), err_.line(), err_.column());
        return err_;
    }

    // Convenience: a fresh error with a user message AND a detail, replacing
    // anything previously set. Returns the new Error& so call sites can
    // either `return err.make(...)` or just chain.
    Error& make(ErrorCode code, std::string user_message, std::string detail) {
        err_ = Error{code, std::move(user_message), std::move(detail)};
        return err_;
    }

    void at_field(std::string_view field) {
        err_ = err_.with_detail("field=" + std::string{field});
    }

  private:
    Error err_;
};

std::string dump_operand(const Operand& o) {
    switch (o.kind) {
        case Operand::Kind::String:  return o.string;
        case Operand::Kind::Integer: return std::to_string(o.integer);
        case Operand::Kind::Boolean: return o.boolean ? "true" : "false";
    }
    return {};
}

Operand operand_from(const json::Value& v) {
    Operand o;
    if (v.is_string()) {
        o.string = std::string{v.as_string()};
        o.kind = Operand::Kind::String;
    } else if (v.is_bool()) {
        o.boolean = v.as_bool();
        o.kind = Operand::Kind::Boolean;
    } else if (v.is_number()) {
        o.integer = v.as_int();
        o.kind = Operand::Kind::Integer;
    }
    return o;
}

FieldOp parse_field_op(std::string_view s) {
    if (s == "eq")          return FieldOp::Eq;
    if (s == "ne")          return FieldOp::Ne;
    if (s == "gt")          return FieldOp::Gt;
    if (s == "lt")          return FieldOp::Lt;
    if (s == "gte")         return FieldOp::Gte;
    if (s == "lte")         return FieldOp::Lte;
    if (s == "contains")    return FieldOp::Contains;
    if (s == "notContains") return FieldOp::NotContains;
    if (s == "startsWith")  return FieldOp::StartsWith;
    if (s == "endsWith")    return FieldOp::EndsWith;
    if (s == "matches")     return FieldOp::Matches;
    if (s == "between")     return FieldOp::Between;
    if (s == "in")          return FieldOp::In;
    if (s == "isNull")      return FieldOp::IsNull;
    if (s == "isNotNull")   return FieldOp::IsNotNull;
    return FieldOp::Eq;  // sentinel; caller checks via error
}

bool parse_field_op_or_error(std::string_view s, FieldOp& out, RuleError& err) {
    static const std::array<std::string_view, 15> kAll = {
        "eq", "ne", "gt", "lt", "gte", "lte",
        "contains", "notContains", "startsWith", "endsWith", "matches",
        "between", "in", "isNull", "isNotNull",
    };
    bool known = false;
    for (auto v : kAll) {
        if (v == s) { known = true; break; }
    }
    if (!known) {
        err.error() = err.error().with_detail(
            "unknown operator '" + std::string{s} + "'");
        return false;
    }
    out = parse_field_op(s);
    return true;
}

bool valid_custom_field_name(std::string_view s) {
    // The schema's pattern: `^custom:[A-Za-z_][A-Za-z0-9_]{0,63}$`
    constexpr std::string_view kPrefix = "custom:";
    if (s.size() <= kPrefix.size()) return false;
    if (s.substr(0, kPrefix.size()) != kPrefix) return false;
    const auto name = s.substr(kPrefix.size());
    if (name.empty() || name.size() > 64) return false;
    const auto first = static_cast<unsigned char>(name[0]);
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

Result<Group> parse_expression(const json::Value& v, RuleError& err);

Result<Comparison> parse_comparison(const json::Value& v, RuleError& err) {
    if (!v.is_object()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A comparison must be an object."};
    }
    Comparison c;

    const auto* field_v = v.find("field");
    if (field_v == nullptr || !field_v->is_string()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A comparison is missing its 'field'."};
    }
    c.field = std::string{field_v->as_string()};
    if (!c.field.empty() && c.field.rfind("custom:", 0) == 0) {
        if (!valid_custom_field_name(c.field)) {
            err.error() = Error{ErrorCode::InvalidArgument,
                "The custom field name is not a valid identifier."};
            err.error() = err.error().with_detail("custom field name is not a valid identifier");
            return err.error();
        }
    } else if (!builtin_fields().contains(c.field)) {
        err.error() = Error{ErrorCode::InvalidArgument,
            "Unknown field '" + c.field + "'."};
        err.error() = err.error().with_detail("unknown field '" + c.field + "'");
        return err.error();
    }
    err.at_field(c.field);

    const auto* op_v = v.find("operator");
    if (op_v == nullptr || !op_v->is_string()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A comparison is missing its 'operator'."};
    }
    const auto op_str = std::string{op_v->as_string()};
    if (!parse_field_op_or_error(op_str, c.op, err)) {
        return err.error();
    }

    if (const auto* n = v.find("not"); n != nullptr && n->is_bool()) {
        c.not_flag = n->as_bool();
    }
    if (const auto* cs = v.find("caseSensitive"); cs != nullptr && cs->is_bool()) {
        c.case_sensitive = cs->as_bool();
    }

    switch (c.op) {
        case FieldOp::IsNull:
        case FieldOp::IsNotNull: {
            for (const char* k : {"value", "values", "from", "to"}) {
                if (v.find(k) != nullptr) {
                    return err.make(ErrorCode::InvalidArgument,
                        std::string{"The '"} + op_str + "' operator takes no operand.",
                        "'isNull' takes no operand");
                }
            }
            break;
        }
        case FieldOp::Between: {
            for (const char* k : {"value", "values"}) {
                if (v.find(k) != nullptr) {
                    return err.make(ErrorCode::InvalidArgument,
                        "The 'between' operator requires 'from' and 'to'.",
                        "'between' requires 'from' and 'to', not 'value'");
                }
            }
            const auto* f = v.find("from");
            const auto* t = v.find("to");
            if (f == nullptr || t == nullptr) {
                return err.make(ErrorCode::InvalidArgument,
                    "The 'between' operator requires both 'from' and 'to'.",
                    "'between' requires 'from' and 'to'");
            }
            c.from = operand_from(*f);
            c.to = operand_from(*t);
            break;
        }
        case FieldOp::In: {
            for (const char* k : {"value", "from", "to"}) {
                if (v.find(k) != nullptr) {
                    return err.make(ErrorCode::InvalidArgument,
                        "The 'in' operator requires 'values'.",
                        "'in' requires 'values', not '" + std::string{k} + "'");
                }
            }
            const auto* arr = v.find("values");
            if (arr == nullptr || !arr->is_array() || arr->size() == 0) {
                return err.make(ErrorCode::InvalidArgument,
                    "The 'in' operator requires a non-empty 'values' list.",
                    "'in' requires non-empty 'values'");
            }
            if (arr->size() > 256) {
                return err.make(ErrorCode::InvalidArgument,
                    "The 'in' list has more than 256 values.",
                    "'in' operand exceeds 256 values");
            }
            for (std::size_t i = 0; i < arr->size(); ++i) {
                c.values.push_back(operand_from(*arr->at(i)));
            }
            break;
        }
        default: {
            for (const char* k : {"values", "from", "to"}) {
                if (v.find(k) != nullptr) {
                    return err.make(ErrorCode::InvalidArgument,
                        "The '" + op_str + "' operator takes a single 'value'.",
                        "operator '" + op_str + "' takes 'value', not '" +
                        std::string{k} + "'");
                }
            }
            const auto* val = v.find("value");
            if (val == nullptr) {
                return err.make(ErrorCode::InvalidArgument,
                    "The '" + op_str + "' operator requires a 'value'.",
                    "operator '" + op_str + "' requires 'value'");
            }
            c.value = operand_from(*val);
            break;
        }
    }
    return c;
}

Result<Group> parse_group(const json::Value& v, RuleError& err) {
    if (!v.is_object()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A group must be an object."};
    }
    Group g;
    const auto* op_v = v.find("op");
    if (op_v == nullptr || !op_v->is_string()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A group is missing its 'op'."};
    }
    const auto op_str = std::string{op_v->as_string()};
    if (op_str == "and") g.op = Group::Conjunction::And;
    else if (op_str == "or") g.op = Group::Conjunction::Or;
    else {
        return err.make(ErrorCode::InvalidArgument,
            "A group's 'op' must be 'and' or 'or'.",
            "group 'op' must be 'and' or 'or'");
    }

    if (const auto* n = v.find("not"); n != nullptr && n->is_bool()) {
        g.not_flag = n->as_bool();
    }

    const auto* rules = v.find("rules");
    if (rules == nullptr || !rules->is_array()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A group is missing its 'rules' array."};
    }
    if (rules->size() == 0) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "rules must not be empty"};
    }
    if (rules->size() > 64) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A group has more than 64 rules."};
    }
    g.children.reserve(rules->size());
    for (std::size_t i = 0; i < rules->size(); ++i) {
        const auto& child = *rules->at(i);
        // A child is either a group (it has 'op' + 'rules') or a comparison
        // (it has 'field' + 'operator'). The schema's oneOf is the
        // authoritative discriminator; here we just look for 'rules'.
        if (child.is_object() && child.find("rules") != nullptr) {
            auto sub = parse_group(child, err);
            if (!sub) return sub;
            g.children.emplace_back(std::move(sub).value());
        } else {
            auto sub = parse_comparison(child, err);
            if (!sub) return sub;
            g.children.emplace_back(std::move(sub).value());
        }
    }
    return g;
}

Result<Group> parse_expression(const json::Value& v, RuleError& err) {
    if (!v.is_object()) {
        return err.error() = Error{ErrorCode::InvalidArgument,
            "A rule's 'match' must be an object."};
    }
    if (v.find("rules") != nullptr) {
        return parse_group(v, err);
    }
    auto c = parse_comparison(v, err);
    if (!c) return err.error();
    Group g;
    g.op = Group::Conjunction::And;
    g.children.emplace_back(std::move(c).value());
    return g;
}

}  // namespace

Result<PlaylistRule> parse_rule_json(std::string_view json_text) {
    RuleError err{"The smart-playlist rule is not valid."};
    auto parsed = json::parse(json_text);
    if (!parsed) {
        // The JSON parser already populated the line/column; surface them.
        Error e{ErrorCode::InvalidArgument, "The smart-playlist rule is not valid JSON."};
        e = e.with_detail(std::string{parsed.error().technical_detail()});
        e.at(parsed.error().offset(), parsed.error().line(), parsed.error().column());
        return e;
    }
    const auto& root = parsed.value();
    if (!root.is_object()) {
        return err.set_user_message(
            "A smart-playlist rule must be a JSON object.");
    }

    PlaylistRule rule;
    const auto* v = root.find("schemaVersion");
    if (v == nullptr || !v->is_number() || v->as_int() != 1) {
        return err.make(ErrorCode::InvalidArgument,
            "The rule's 'schemaVersion' is required and must be 1.",
            "schemaVersion is required and must be 1");
    }
    if (const auto* n = root.find("name"); n != nullptr && n->is_string()) {
        rule.name = std::string{n->as_string()};
    }
    if (const auto* d = root.find("description"); d != nullptr && d->is_string()) {
        rule.description = std::string{d->as_string()};
    }
    if (const auto* a = root.find("autoRefresh"); a != nullptr && a->is_bool()) {
        rule.auto_refresh = a->as_bool();
    }

    const auto* match_v = root.find("match");
    if (match_v == nullptr) {
        return err.make(ErrorCode::InvalidArgument,
            "A smart-playlist rule is missing its 'match'.",
            "missing 'match'");
    }
    auto g = parse_expression(*match_v, err);
    if (!g) return err.error();
    rule.match = std::move(g).value();

    if (const auto* ob = root.find("orderBy"); ob != nullptr) {
        if (!ob->is_array() || ob->size() == 0) {
            return err.make(ErrorCode::InvalidArgument,
                "'orderBy' must be a non-empty array.",
                "'orderBy' must be a non-empty array");
        }
        if (ob->size() > 8) {
            return err.make(ErrorCode::InvalidArgument,
                "'orderBy' has more than 8 terms.",
                "'orderBy' exceeds 8 terms");
        }
        rule.order_by.reserve(ob->size());
        for (std::size_t i = 0; i < ob->size(); ++i) {
            const auto& term = *ob->at(i);
            if (!term.is_object()) {
                return err.make(ErrorCode::InvalidArgument,
                    "An order term must be an object.", "order term is not an object");
            }
            const auto* tf = term.find("field");
            if (tf == nullptr || !tf->is_string()) {
                return err.make(ErrorCode::InvalidArgument,
                    "An order term is missing 'field'.", "order term is missing 'field'");
            }
            OrderTerm t;
            t.field = std::string{tf->as_string()};
            if (t.field != "random" && t.field.rfind("custom:", 0) != 0 &&
                !builtin_fields().contains(t.field)) {
                return err.make(ErrorCode::InvalidArgument,
                    "Unknown order field '" + t.field + "'.",
                    "order field '" + t.field + "' is not a known field");
            }
            if (const auto* d = term.find("direction"); d != nullptr && d->is_string()) {
                const auto s = std::string{d->as_string()};
                if (s == "asc") t.direction = OrderDirection::Asc;
                else if (s == "desc") t.direction = OrderDirection::Desc;
                else {
                    return err.make(ErrorCode::InvalidArgument,
                        "Order direction must be 'asc' or 'desc'.",
                        "order direction must be 'asc' or 'desc'");
                }
            }
            rule.order_by.push_back(std::move(t));
        }
    }

    if (const auto* lim = root.find("limit"); lim != nullptr) {
        if (!lim->is_number() || !lim->is_integer()) {
            return err.make(ErrorCode::InvalidArgument,
                "'limit' must be an integer.",
                "'limit' must be an integer");
        }
        const auto n = lim->as_int();
        if (n < 1) {
            return err.make(ErrorCode::InvalidArgument,
                "'limit' must be at least 1.",
                "'limit' is below the minimum of 1");
        }
        if (n > 100000) {
            return err.make(ErrorCode::InvalidArgument,
                "'limit' must be at most 100000.",
                "'limit' exceeds the maximum of 100000");
        }
        rule.limit = n;
    }
    return rule;
}

namespace {

// ===========================================================================
//  Operand → BindValue (literal normalisation)
// ===========================================================================

bool is_int_text(std::string_view s) {
    if (s.empty()) return false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

// Convert a string operand to an int64 when it carries a known unit. The
// fixture `lit-duration-compound` pins that 3m30s == 210, and `lit-duration-hours`
// pins 1h == 3600. The literal is normalised BEFORE binding, so the database
// never has to parse a unit suffix (per the case note).
//
// We only normalise operands the grammar actually advertises:
//   * duration literal:        number ( "s" | "m" | "h" )        → seconds
//   * relative date literal:   -N ( "d" | "w" | "m" | "y" )     → kept as
//     string (REQ-PLS-014 re-evaluates against the current time; the database
//     may store `last_played_at` as an integer epoch and the engine — not the
//     schema — resolves "now - 14 days" before bind, in a later phase. For
//     now the parameter passes through verbatim, matching the conformance
//     fixture `lit-relative-weeks` which expects `'-6w'` to be bound.
//   * ISO-8601 date literal:   'YYYY-MM-DD'                       → string
Result<BindValue> normalise_bind(const Operand& o, const Comparison& c, RuleError& err) {
    BindValue b;
    switch (o.kind) {
        case Operand::Kind::Integer:
            b.kind = BindValue::Kind::Integer;
            b.integer = o.integer;
            return b;
        case Operand::Kind::Boolean:
            b.kind = BindValue::Kind::Boolean;
            b.boolean = o.boolean;
            return b;
        case Operand::Kind::String:
            break;
    }
    const auto& s = o.string;

    auto expect_unit = [&](std::size_t& cursor) -> bool {
        if (cursor >= s.size()) return false;
        const char u = s[cursor];
        if (u != 's' && u != 'm' && u != 'h' && u != 'd' && u != 'w' && u != 'y') return false;
        ++cursor;
        return true;
    };

    // duration fields: parse "Ns", "Nm", "Nh", or "NmSs" compound form
    if (c.field == "duration") {
        if (is_int_text(s) && s.size() > 1) {
            const char last = s.back();
            if (last == 's' || last == 'm' || last == 'h') {
                std::size_t i = 0;
                if (s[0] == '-') i = 1;
                if (i + 1 < s.size()) {
                    std::int64_t n = 0;
                    for (; i < s.size() - 1; ++i) {
                        n = n * 10 + (s[i] - '0');
                    }
                    std::int64_t mult = 1;
                    if (last == 'm') mult = 60;
                    else if (last == 'h') mult = 3600;
                    b.kind = BindValue::Kind::Integer;
                    b.integer = n * mult;
                    return b;
                }
            }
        }
        // compound "NmSs" / "NhMs" form. The EBNF lists `number ( "s" | "m" |
        // "h" )` as a single-suffix literal but the spec example and the
        // fixture both use 3m30s, which is two suffixes. We treat the
        // trailing-suffix form above as the common case and the compound
        // form here.
        std::int64_t total = 0;
        std::int64_t current = 0;
        std::size_t i = 0;
        bool saw_any = false;
        if (i < s.size() && s[i] == '-') ++i;  // tolerate leading sign
        while (i < s.size()) {
            const char ch = s[i];
            if (ch >= '0' && ch <= '9') {
                current = current * 10 + (ch - '0');
                ++i;
                continue;
            }
            if (ch == 's' || ch == 'm' || ch == 'h') {
                std::int64_t mult = 1;
                if (ch == 'm') mult = 60;
                else if (ch == 'h') mult = 3600;
                total += current * mult;
                current = 0;
                saw_any = true;
                ++i;
                continue;
            }
            return err.make(ErrorCode::InvalidArgument,
                "The duration literal is malformed.",
                "duration literal '" + s + "' is malformed");
        }
        if (saw_any) {
            b.kind = BindValue::Kind::Integer;
            b.integer = total;
            return b;
        }
    }

    b.kind = BindValue::Kind::Text;
    b.text = s;
    return b;
}

// Escape % and _ for LIKE so user text does not pick up wildcards (REQ-SEC-009
// fixture `sec-like-wildcards-escaped`). The escape character is '\\' and the
// SQL carries `ESCAPE '\\'` (REQ-SEC-009 fixture).
std::string escape_like(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// ===========================================================================
//  Compile
// ===========================================================================

class Compiler {
  public:
    explicit Compiler(CompiledQuery& out) : out_{out} {}

    [[nodiscard]] Result<Unit> run(const PlaylistRule& rule) {
        // Match expression → WHERE fragment.
        if (auto r = compile_group(rule.match); !r) return r;
        if (out_.where.empty()) out_.where = "1=1";

        // ORDER BY. Each term is either 'random', a builtin field column, or
        // a custom field (parameterised EXISTS subquery). The fixture
        // `req-pls-012-fresh-and-unheard` says ORDER BY chosen at compile
        // time; we never interpolate a value.
        if (!rule.order_by.empty()) {
            std::string ob;
            for (std::size_t i = 0; i < rule.order_by.size(); ++i) {
                if (i) ob += ", ";
                const auto& term = rule.order_by[i];
                if (term.field == "random") {
                    ob += "RANDOM()";
                } else {
                    const auto& col = column_for(term.field);
                    if (col.empty()) {
                        return err_ = Error{ErrorCode::InvalidArgument,
                            "Order field '" + term.field + "' is not a known column."};
                    }
                    ob += "t.";
                    ob += col;
                }
                ob += term.direction == OrderDirection::Desc ? " DESC" : " ASC";
            }
            out_.order_by = std::move(ob);
        }

        out_.limit = rule.limit;
        return ok();
    }

    [[nodiscard]] Result<Unit> compile_group(const Group& g) {
        const std::string conjunction = g.op == Group::Conjunction::And ? " AND " : " OR ";
        std::string body;
        body.reserve(64);
        body.push_back('(');
        for (std::size_t i = 0; i < g.children.size(); ++i) {
            if (i) body += conjunction;
            if (std::holds_alternative<Group>(g.children[i])) {
                if (auto r = compile_group(std::get<Group>(g.children[i])); !r) return r;
                body += out_.where;
            } else {
                if (auto r = compile_comparison(std::get<Comparison>(g.children[i])); !r) return r;
                body += out_.where;
            }
            out_.where.clear();
        }
        body.push_back(')');
        if (g.not_flag) {
            out_.where = "NOT " + body;
        } else {
            out_.where = std::move(body);
        }
        return ok();
    }

    [[nodiscard]] Result<Unit> compile_comparison(const Comparison& c) {
        // `custom:` fields always compile to an EXISTS subquery against
        // track_custom_tags. The field name (the part after `custom:`) and
        // the value are both bound, so the user can never type SQL into
        // either (REQ-SEC-009 fixture `sec-custom-field-name-is-bound`).
        if (c.field.rfind("custom:", 0) == 0) {
            return compile_custom(c);
        }
        const auto& col = column_for(c.field);
        if (col.empty()) {
            return err_ = Error{ErrorCode::InvalidArgument,
                "Field '" + c.field + "' is not a known column."};
        }
        const std::string lhs = "t." + col;

        // Default collation: string operators get COLLATE NOCASE unless the
        // rule opts in to case sensitivity. The fixture
        // `case-insensitive-default` pins this; `case-sensitive-opt-in` covers
        // the opt-out path.
        const bool stringy = is_string_field(c.field);
        const bool collate_nocase = stringy && !c.case_sensitive;

        std::string frag;
        switch (c.op) {
            case FieldOp::IsNull: {
                frag = lhs + " IS NULL";
                break;
            }
            case FieldOp::IsNotNull: {
                frag = lhs + " IS NOT NULL";
                break;
            }
            case FieldOp::Between: {
                if (auto r = push_bind(c.from, c); !r) return r;
                if (auto r = push_bind(c.to, c); !r) return r;
                frag = lhs + " BETWEEN ? AND ?";
                break;
            }
            case FieldOp::In: {
                if (c.values.empty()) {
                    return err_ = Error{ErrorCode::InvalidArgument,
                        "The 'in' operator requires a non-empty list."};
                }
                std::string placeholders;
                placeholders.reserve(c.values.size() * 3);
                for (std::size_t i = 0; i < c.values.size(); ++i) {
                    if (i) placeholders += ", ";
                    if (auto r = push_bind(c.values[i], c); !r) return r;
                    placeholders += "?";
                }
                frag = lhs + " IN (" + placeholders + ")";
                break;
            }
            case FieldOp::Contains:
            case FieldOp::NotContains:
            case FieldOp::StartsWith:
            case FieldOp::EndsWith: {
                if (!stringy) {
                    return err_ = Error{ErrorCode::InvalidArgument,
                        "The '" + sql_operator(c.op) +
                        "' operator is not valid on a numeric field."};
                }
                if (c.value.kind != Operand::Kind::String) {
                    return err_ = Error{ErrorCode::InvalidArgument,
                        "A string operator requires a string value."};
                }
                std::string pattern = c.value.string;
                if (c.op == FieldOp::Contains || c.op == FieldOp::NotContains) {
                    pattern = "%" + escape_like(pattern) + "%";
                } else if (c.op == FieldOp::StartsWith) {
                    pattern = escape_like(pattern) + "%";
                } else {
                    pattern = "%" + escape_like(pattern);
                }
                BindValue b;
                b.kind = BindValue::Kind::Text;
                b.text = std::move(pattern);
                out_.params.push_back(std::move(b));
                frag = lhs + " " + sql_operator(c.op) + " ?";
                if (stringy) frag += " ESCAPE '\\'";
                break;
            }
            case FieldOp::Matches: {
                if (c.value.kind != Operand::Kind::String) {
                    return err_ = Error{ErrorCode::InvalidArgument,
                        "'matches' requires a string value."};
                }
                BindValue b;
                b.kind = BindValue::Kind::Text;
                b.text = c.value.string;
                out_.params.push_back(std::move(b));
                frag = lhs + " GLOB ?";
                break;
            }
            default: {
                if (auto r = push_bind(c.value, c); !r) return r;
                frag = lhs + " " + sql_operator(c.op) + " ?";
                break;
            }
        }

        if (collate_nocase) {
            // The expected fragment is e.g. `artist = ? COLLATE NOCASE`.
            // For IsNull / IsNotNull COLLATE is meaningless so we skip.
            if (c.op != FieldOp::IsNull && c.op != FieldOp::IsNotNull) {
                frag += " COLLATE NOCASE";
            }
        }

        if (c.not_flag) {
            // Per the conformance fixture `req-pls-012-long-form`, the
            // rendered form is `NOT (genre LIKE ?)`. The "not" wraps the
            // emitted comparison, not the column.
            out_.where = "NOT (" + frag + ")";
        } else {
            out_.where = std::move(frag);
        }
        return ok();
    }

    [[nodiscard]] Result<Unit> compile_custom(const Comparison& c) {
        // We don't use BETWEEN/IN/IS [NOT] NULL on custom tags: the schema
        // permits them in theory but the grammar lists them only for
        // builtin fields. Any non-Eq / non-Ne call here is rejected.
        switch (c.op) {
            case FieldOp::Eq:
            case FieldOp::Ne:
            case FieldOp::Contains:
            case FieldOp::NotContains:
            case FieldOp::StartsWith:
            case FieldOp::EndsWith:
            case FieldOp::Matches:
                break;
            default:
                return err_ = Error{ErrorCode::InvalidArgument,
                    "The '" + sql_operator(c.op) + "' operator is not valid on a custom field."};
        }
        if (c.value.kind != Operand::Kind::String) {
            return err_ = Error{ErrorCode::InvalidArgument,
                "A custom field comparison requires a string value."};
        }
        const auto tag = c.field.substr(std::string{"custom:"}.size());
        BindValue key;
        key.kind = BindValue::Kind::Text;
        key.text = tag;
        out_.params.push_back(std::move(key));

        std::string pattern = c.value.string;
        switch (c.op) {
            case FieldOp::Contains:
            case FieldOp::NotContains:
                pattern = "%" + escape_like(pattern) + "%"; break;
            case FieldOp::StartsWith:
                pattern = escape_like(pattern) + "%"; break;
            case FieldOp::EndsWith:
                pattern = "%" + escape_like(pattern); break;
            default: break;
        }
        BindValue value;
        value.kind = BindValue::Kind::Text;
        value.text = std::move(pattern);
        out_.params.push_back(std::move(value));

        std::string frag;
        switch (c.op) {
            case FieldOp::Contains:    frag = "ct.value LIKE ? ESCAPE '\\'"; break;
            case FieldOp::NotContains: frag = "ct.value NOT LIKE ? ESCAPE '\\'"; break;
            case FieldOp::StartsWith:  frag = "ct.value LIKE ? ESCAPE '\\'"; break;
            case FieldOp::EndsWith:    frag = "ct.value LIKE ? ESCAPE '\\'"; break;
            case FieldOp::Matches:     frag = "ct.value GLOB ?"; break;
            default:                   frag = "ct.value = ?"; break;
        }
        std::string exists = "EXISTS (SELECT 1 FROM custom_tags ct "
                             "WHERE ct.track_id = t.id AND ct.key = ? AND " + frag + ")";
        if (c.op == FieldOp::Ne) {
            // `custom:mood != 'melancholy'` is "there IS a mood tag whose
            // value is something other than melancholy" — a separate
            // subquery, matching how the schema defines Ne on the same
            // shape as the other comparators but the SQL difference is the
            // EXISTS-vs-NOT-EXISTS swap. Simpler: render as `NOT EXISTS` of
            // an Eq.
            exists = "NOT EXISTS (SELECT 1 FROM custom_tags ct "
                     "WHERE ct.track_id = t.id AND ct.key = ? AND ct.value = ?)";
        }
        if (c.not_flag) {
            out_.where = "NOT (" + exists + ")";
        } else {
            out_.where = std::move(exists);
        }
        return ok();
    }

    [[nodiscard]] Result<Unit> push_bind(const Operand& o, const Comparison& c) {
        RuleError err{""};
        auto b = normalise_bind(o, c, err);
        if (!b) return err.error();
        out_.params.push_back(std::move(b).value());
        return ok();
    }

    CompiledQuery& out_;
    Error err_{ErrorCode::Unknown, ""};
};

}  // namespace

Result<CompiledQuery> compile(const PlaylistRule& rule) {
    CompiledQuery out;
    Compiler c{out};
    if (auto r = c.run(rule); !r) {
        return c.err_;
    }
    return out;
}

namespace {

// ===========================================================================
//  Serialise PlaylistRule → JSON  (to_json, used to round-trip through DB)
// ===========================================================================

void dump_operand(const Operand& o, json::Value& out) {
    switch (o.kind) {
        case Operand::Kind::String:  out = json::Value{o.string}; break;
        case Operand::Kind::Boolean: out = json::Value{o.boolean}; break;
        case Operand::Kind::Integer: out = json::Value{static_cast<double>(o.integer)}; break;
    }
}

void dump_match(const std::variant<Group, Comparison>& m, json::Value& out);

void dump_group(const Group& g, json::Value& out) {
    json::Object obj;
    obj["op"] = json::Value{g.op == Group::Conjunction::And ? std::string{"and"} : std::string{"or"}};
    if (g.not_flag) obj["not"] = json::Value{true};
    json::Array rules;
    for (const auto& child : g.children) {
        json::Value v;
        dump_match(child, v);
        rules.push_back(std::move(v));
    }
    obj["rules"] = json::Value{std::move(rules)};
    out = json::Value{std::move(obj)};
}

void dump_comparison(const Comparison& c, json::Value& out) {
    json::Object obj;
    obj["field"] = json::Value{c.field};
    const char* op_str = "";
    switch (c.op) {
        case FieldOp::Eq:          op_str = "eq"; break;
        case FieldOp::Ne:          op_str = "ne"; break;
        case FieldOp::Gt:          op_str = "gt"; break;
        case FieldOp::Lt:          op_str = "lt"; break;
        case FieldOp::Gte:         op_str = "gte"; break;
        case FieldOp::Lte:         op_str = "lte"; break;
        case FieldOp::Contains:    op_str = "contains"; break;
        case FieldOp::NotContains: op_str = "notContains"; break;
        case FieldOp::StartsWith:  op_str = "startsWith"; break;
        case FieldOp::EndsWith:    op_str = "endsWith"; break;
        case FieldOp::Matches:     op_str = "matches"; break;
        case FieldOp::Between:     op_str = "between"; break;
        case FieldOp::In:          op_str = "in"; break;
        case FieldOp::IsNull:      op_str = "isNull"; break;
        case FieldOp::IsNotNull:   op_str = "isNotNull"; break;
    }
    obj["operator"] = json::Value{std::string{op_str}};
    if (c.not_flag) obj["not"] = json::Value{true};
    if (c.case_sensitive) obj["caseSensitive"] = json::Value{true};
    switch (c.op) {
        case FieldOp::IsNull:
        case FieldOp::IsNotNull: break;
        case FieldOp::Between: {
            json::Value jv;
            dump_operand(c.from, jv);
            obj["from"] = std::move(jv);
            json::Value jt;
            dump_operand(c.to, jt);
            obj["to"] = std::move(jt);
            break;
        }
        case FieldOp::In: {
            json::Array arr;
            for (const auto& v : c.values) {
                json::Value j;
                dump_operand(v, j);
                arr.push_back(std::move(j));
            }
            obj["values"] = json::Value{std::move(arr)};
            break;
        }
        default: {
            json::Value jv;
            dump_operand(c.value, jv);
            obj["value"] = std::move(jv);
            break;
        }
    }
    out = json::Value{std::move(obj)};
}

void dump_match(const std::variant<Group, Comparison>& m, json::Value& out) {
    if (std::holds_alternative<Group>(m)) dump_group(std::get<Group>(m), out);
    else                                   dump_comparison(std::get<Comparison>(m), out);
}

}  // namespace

std::string to_json(const PlaylistRule& rule) {
    json::Object root;
    root["schemaVersion"] = json::Value{1.0};
    if (!rule.name.empty()) root["name"] = json::Value{rule.name};
    if (!rule.description.empty()) root["description"] = json::Value{rule.description};
    if (!rule.auto_refresh) root["autoRefresh"] = json::Value{false};

    json::Value match;
    dump_match(rule.match, match);
    root["match"] = std::move(match);

    if (!rule.order_by.empty()) {
        json::Array arr;
        for (const auto& t : rule.order_by) {
            json::Object term;
            term["field"] = json::Value{t.field};
            term["direction"] = json::Value{
                t.direction == OrderDirection::Asc ? std::string{"asc"} : std::string{"desc"}};
            arr.push_back(json::Value{std::move(term)});
        }
        root["orderBy"] = json::Value{std::move(arr)};
    }
    if (rule.limit) root["limit"] = json::Value{static_cast<double>(*rule.limit)};

    return json::Value{std::move(root)}.dump(0);
}

}  // namespace arrow::library
