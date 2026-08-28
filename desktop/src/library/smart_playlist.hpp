// SPDX-License-Identifier: MPL-2.0
// Smart-playlist compiler — spec §9.6, REQ-PLS-010 .. REQ-PLS-015.
//
// A smart playlist is a JSON document that matches the schema at
// shared-spec/schemas/smart-playlist.schema.json. The compiler turns that
// document into a parameterised SQL statement: every literal becomes a bound
// parameter, every column name and operator is picked from a closed set at
// compile time, and the only string ever reaching the database engine is the
// user's value carried as a parameter. That is what REQ-SEC-009 demands and
// what tools/check-sql-safety.py enforces.
//
// This file is the only place in the library layer that emits SQL beyond the
// fixed queries in library_database.cpp. The grammar is the one in
// shared-spec/grammars/smart-playlist.ebnf; the conformance fixtures are in
// shared-spec/conformance/smart-playlist-cases.json; the spec section is
// eclipse-player.md §9.6.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.hpp"

namespace arrow::library {

// ---------------------------------------------------------------------------
//  Rule model
// ---------------------------------------------------------------------------

/// A literal a rule was written with. Number and boolean are stored as their
/// natural type; string keeps the textual form so duration / date / relative
/// literals can be normalised by the compiler and surface as int64 at bind
/// time (the database never has to parse a unit suffix — see
/// `neglected-favourites` and `lit-duration-compound` in the conformance set).
struct Operand final {
    std::string string;     ///< raw text when `kind` is `String`; "" otherwise
    std::int64_t integer{0};  ///< numeric value when `kind` is `Integer`
    bool boolean{false};    ///< value when `kind` is `Boolean`
    enum class Kind { String, Integer, Boolean } kind{Kind::String};
};

enum class FieldOp {
    Eq, Ne, Gt, Lt, Gte, Lte,
    Contains, NotContains, StartsWith, EndsWith, Matches,
    Between, In, IsNull, IsNotNull
};

/// One comparison. The `field` is a builtin name or `custom:<identifier>`
/// (the schema's identifier pattern is the only validator for the latter —
/// this module is the validator; see `parse_comparison`).
struct Comparison final {
    std::string field;
    FieldOp op{FieldOp::Eq};
    bool not_flag{false};        ///< per-row `not`
    bool case_sensitive{false};  ///< string operators only; default insensitive
    Operand value;               ///< used unless op is Between / In / IsNull / IsNotNull
    Operand from;                ///< used by Between
    Operand to;                  ///< used by Between
    std::vector<Operand> values; ///< used by In
};

/// A group (AND/OR). Empty groups are a schema error (`rules must not be empty`).
struct Group final {
    enum class Conjunction { And, Or } op{Conjunction::And};
    bool not_flag{false};
    std::vector<std::variant<Group, Comparison>> children;
};

enum class OrderDirection { Asc, Desc };

struct OrderTerm final {
    std::string field;            ///< a builtin field, `custom:<id>`, or "random"
    OrderDirection direction{OrderDirection::Asc};
};

/// A complete smart-playlist rule. The `match` is a tree of groups and
/// comparisons; `order_by` and `limit` are structure, never user values.
struct PlaylistRule final {
    std::string name;
    std::string description;
    Group match;
    std::vector<OrderTerm> order_by;
    std::optional<std::int64_t> limit;  ///< 1..100000 per schema
    bool auto_refresh{true};
};

// ---------------------------------------------------------------------------
//  Compiled query
// ---------------------------------------------------------------------------

/// One bound parameter, tagged so the caller can hand it to sqlite3_bind_*
/// without re-parsing. Int64 covers durations, dates-as-epoch and BpM;
/// doubles are reserved for ratings / replaygain (kept distinct so a future
/// migration to REAL columns does not force a flag day in the compiler).
struct BindValue final {
    enum class Kind { Text, Integer, Real, Boolean } kind{Kind::Text};
    std::string text;
    std::int64_t integer{0};
    double real{0.0};
    bool boolean{false};
};

/// The compile output: a SQL fragment (where-clause) and a parameter list
/// in the order the `?` placeholders appear. The caller wraps it:
///   SELECT ... FROM tracks t WHERE <fragment> ORDER BY ... LIMIT ?
struct CompiledQuery final {
    std::string where;                  ///< ready to splice into "... WHERE <where>"
    std::vector<BindValue> params;      ///< one per `?`, in order
    std::string order_by{"t.id ASC"};   ///< default order; `random` and custom fields are appended
    std::optional<std::int64_t> limit;  ///< number; bound by the caller as `?`
};

/// Compile a rule to a parameterised SQL fragment. The fragment is intended
/// to be dropped into `SELECT ... FROM tracks t WHERE <where> ORDER BY <ob> LIMIT <n>`.
/// On schema/grammar errors the returned `Error` carries the field name (or
/// "match" for the root) and a 1-indexed line/column so the editor can point
/// at the offending node.
[[nodiscard]] Result<CompiledQuery> compile(const PlaylistRule& rule);

/// Parse the JSON form. The schema and the JSON grammar are the same shape,
/// so this is mostly a structural walk; the schema is the authoritative
/// validator and any disagreement is reported here too (REQ-PLS-015: the text
/// grammar and the JSON form stay in sync, the JSON is canonical).
[[nodiscard]] Result<PlaylistRule> parse_rule_json(std::string_view json_text);

/// Recursively serialise a rule back to JSON. Used by the database adapter
/// to round-trip rules through `playlists.rule_json` (REQ-LIB-050). The shape
/// matches the schema's canonical form byte-for-byte, modulo whitespace.
[[nodiscard]] std::string to_json(const PlaylistRule& rule);

}  // namespace arrow::library
