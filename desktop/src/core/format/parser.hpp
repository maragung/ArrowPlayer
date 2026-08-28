// SPDX-License-Identifier: MPL-2.0
// EFS (Eclipse Format Strings) — hand-written recursive-descent parser.
//
// Spec: eclipse-player.md §10.2 (REQ-EFS-002).  Grammar:
// shared-spec/grammars/eclipse-format-strings.ebnf.
//
// The parser is intentionally tiny and pure: it produces an AST and a
// per-pattern list of parse problems; evaluation is a separate phase
// (evaluator.hpp).  No exceptions are thrown across the public surface
// (REQ-EFS-006: errors must never crash, never block, never blank).
//
// Recovery model: when the parser hits a token it cannot accept, it
// returns the literal source range as a `LiteralText` node.  This is
// what makes the malformed cases in conformance/efs-cases.json
// (malformed-unterminated-field, malformed-unterminated-function, …)
// render the remainder rather than blank the surface.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arrow::format {

// ---------------------------------------------------------------------------
//  AST
// ---------------------------------------------------------------------------

enum class FieldSpec {
    None,    // no spec
    Upper,   // :u
    Lower,   // :l
    Title,   // :t
    ZeroPad, // :NN  (width stored in width_)
};

struct FieldRef {
    std::string name;        // identifier between % %
    FieldSpec spec{FieldSpec::None};
    int width{0};            // valid when spec == ZeroPad
    std::size_t start{0};    // byte offset of the leading '%'
    std::size_t end{0};      // byte offset just past the trailing '%'
};

struct FunctionCall {
    std::string name;
    // Each argument is itself a pattern (vector of nodes).
    std::vector<std::vector<class Node>> args;
    std::size_t start{0};
    std::size_t end{0};
};

struct OptionalBlock {
    std::vector<class Node> body;
    std::size_t start{0};
    std::size_t end{0};  // past the closing ']' when present
};

struct LiteralText {
    std::string text;
};

class Node {
  public:
    enum class Kind { Literal, Field, Function, Block };

    Node() = default;
    explicit Node(LiteralText lit) : kind_{Kind::Literal}, lit_{std::move(lit)} {}
    explicit Node(FieldRef f) : kind_{Kind::Field}, field_{std::move(f)} {}
    explicit Node(FunctionCall f) : kind_{Kind::Function}, func_{std::move(f)} {}
    explicit Node(OptionalBlock b) : kind_{Kind::Block}, block_{std::move(b)} {}

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] const LiteralText& literal() const noexcept { return lit_; }
    [[nodiscard]] const FieldRef& field() const noexcept { return field_; }
    [[nodiscard]] const FunctionCall& func() const noexcept { return func_; }
    [[nodiscard]] const OptionalBlock& block() const noexcept { return block_; }

    [[nodiscard]] LiteralText& literal() noexcept { return lit_; }
    [[nodiscard]] FieldRef& field() noexcept { return field_; }
    [[nodiscard]] FunctionCall& func() noexcept { return func_; }
    [[nodiscard]] OptionalBlock& block() noexcept { return block_; }

  private:
    Kind kind_{Kind::Literal};
    LiteralText lit_;
    FieldRef field_;
    FunctionCall func_;
    OptionalBlock block_;
};

using Pattern = std::vector<Node>;

// ---------------------------------------------------------------------------
//  Parse problem — a single, recoverable parser finding.
//
//  The malformed group of conformance/efs-cases.json is the contract: every
//  malformed input still produces a string.  `start` and `end` delimit the
//  source slice that the parser fell back to a literal for; the engine uses
//  them to mark up the editor (REQ-EFS-011).
// ---------------------------------------------------------------------------

struct ParseProblem {
    enum class Code {
        UnterminatedQuotedRun,   // the ' run is closed at end of pattern
        UnterminatedFieldRef,   // no closing '%' before end of pattern
        EmptyFieldRef,          // "%%" — a pair of percent signs
        UnterminatedFunction,   // no closing ')' for a $name(
        UnknownFunction,        // $name where name is not in the closed set
        WrongArity,             // function applied with the wrong arity
        UnterminatedBlock,      // no closing ']' for '['
        UnmatchedCloseBracket,  // a ']' with no matching '['
        LonePercent,            // a '%' with no identifier following
        LoneDollar,             // a '$' that does not start a function
        NestingTooDeep,         // 64 levels exceeded
    };
    Code code{Code::UnterminatedQuotedRun};
    std::size_t start{0};
    std::size_t end{0};
};

// ---------------------------------------------------------------------------
//  parse() — the only public entry point.
//
//  Always returns an AST; on malformed input the AST still describes what to
//  render and `problems` is non-empty.  The engine never sees "no AST at
//  all" — REQ-EFS-006.
// ---------------------------------------------------------------------------

struct ParseResult {
    Pattern pattern;
    std::vector<ParseProblem> problems;
};

[[nodiscard]] ParseResult parse(std::string_view source) noexcept;

}  // namespace arrow::format
