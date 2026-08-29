// SPDX-License-Identifier: MPL-2.0
// EFS evaluator — spec §10.5, REQ-EFS-001 .. REQ-EFS-012.
//
// Two responsibilities live here, separated by header because they belong
// to different layers:
//
//   * TrackView  — the read-only data interface a track exposes to a
//     pattern.  Defined here so the engine can be unit-tested against a
//     stub without depending on the real Track model.
//
//   * Evaluator  — walks a parsed AST against a TrackView and a small
//     amount of evaluation context (locale, "now" instant), and yields a
//     rendered string.  Pure: no I/O, no exceptions across the public
//     surface (REQ-EFS-002), total: every input terminates in time
//     linear in the pattern length (REQ-EFS-002).
//
// Absent-vs-empty: the evaluator returns a Value, which is either a
// present string OR an explicit "absent" marker.  Functions and blocks
// propagate absent without falling back to the empty string; the empty
// string is a *value* that the caller asked for.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/format/parser.hpp"

namespace arrow::efs {

// ---------------------------------------------------------------------------
//  TrackView — what a track offers to a pattern.
//
//  Unit tests provide an in-memory implementation; the real Track from the
//  library layer adapts onto this interface at the composition root.
//  Returning `std::nullopt` means "absent" (REQ-EFS-004); the empty string
//  is a value, not an absence.
// ---------------------------------------------------------------------------

class TrackView {
  public:
    virtual ~TrackView() = default;

    /// Lookup a field by name.  Returns `nullopt` for unknown / unset /
    /// whitespace-only fields (REQ-EFS-004).  Numeric values are returned
    /// in their canonical string form (the same form the column would
    /// show, e.g. `42`, `3.14`, `2000-10-02`).
    [[nodiscard]] virtual std::optional<std::string> field(
        std::string_view name) const noexcept = 0;

    /// Lookup a multi-valued field by name, returning the list in its
    /// stored order.  Returns `nullopt` when the field is absent; an
    /// empty vector means the field is present-but-empty (which is rare
    /// in tag data but kept distinct for symmetry with REQ-LIB-028).
    /// Single-valued fields are returned as a one-element list.
    [[nodiscard]] virtual std::optional<std::vector<std::string>> multi_field(
        std::string_view name) const noexcept = 0;

    /// "Now" for relative-time functions ($age).  The default returns the
    /// unix epoch; tests override it.
    [[nodiscard]] virtual std::int64_t now_unix() const noexcept { return 0; }
};

// ---------------------------------------------------------------------------
//  Evaluation context — what every function call needs that is NOT the
//  TrackView.  Locale governs the decimal separator in $filesize_natural
//  and the formatting of $date / $age.  "now" is a unix timestamp.
// ---------------------------------------------------------------------------

struct EvalContext {
    std::string locale{"en-US"};
    std::int64_t now_unix{0};
    std::size_t output_cap{4096};  // REQ-EFS-009
};

// ---------------------------------------------------------------------------
//  Value — present string OR absent marker.  Optional<string> would do, but
//  a named class lets the dispatcher return Value without a wrapper and
//  keeps the "absent vs empty" distinction obvious in logs.
// ---------------------------------------------------------------------------

class Value {
  public:
    Value() = default;  // absent
    explicit Value(std::string s) : text_{std::move(s)} {}

    [[nodiscard]] bool absent() const noexcept { return !text_.has_value(); }
    [[nodiscard]] const std::string& str() const noexcept { return *text_; }
    [[nodiscard]] std::string take() noexcept { return std::move(*text_); }

    /// True when a field reference resolved to a present value, OR a
    /// function yielded a non-empty result.  Used by the block-collapse
    /// rule (REQ-EFS-003): a block that contains at least one
    /// `is_present` value among its direct-or-transitive field references
    /// renders; otherwise it collapses to "".
    [[nodiscard]] bool is_present() const noexcept {
        return text_.has_value() && !text_->empty();
    }

  private:
    std::optional<std::string> text_;
};

// ---------------------------------------------------------------------------
//  Evaluator — one-shot walker.  Construct, call evaluate() once, throw
//  away.  The internal cursor/state is the kind of thing that wants to be
//  a `friend` of itself across recursive calls, so the class is the
//  natural boundary.
// ---------------------------------------------------------------------------

class Evaluator {
  public:
    Evaluator(const TrackView& track, const EvalContext& ctx) noexcept
        : track_{track}, ctx_{ctx} {}

    /// Evaluate a (parsed) pattern.  The output is built into `out`.  When
    /// the rendered string would exceed the configured output cap, the
    /// evaluator stops appending and reports the cap and the pattern
    /// length back via `cap_reached`.  The engine turns that into a
    /// Status; the evaluator itself never throws.
    void evaluate(const Pattern& pattern, std::string& out,
                  bool* cap_reached = nullptr) noexcept;

  private:
    const TrackView& track_;
    const EvalContext& ctx_;

    Value eval_node(const Node& node) noexcept;

    // Render a block's body.  `parent_is_wrapper` is true when this
    // block is a direct child of another block that has no literals,
    // no present field refs and no function calls — the "invisible
    // wrapper" case the conformance `block-nested-outer-collapses`
    // requires.  In wrapper mode, literals are dropped and nested
    // blocks recurse with `parent_is_wrapper = true`, so the block's
    // output is the values of its present field refs only.
    void render_block(const OptionalBlock& blk, bool parent_is_wrapper,
                      std::string& out) noexcept;

    // Helper: does this subtree contain a field reference that resolves
    // to a present (non-absent, non-empty) value?  Drives the block-
    // collapse rule (REQ-EFS-003).  Walks into function arguments so
    // [$upper(%album%)] is checked against %album%, not the function
    // call itself.
    bool any_present_field(const Pattern& pattern) noexcept;

    // Apply a single function by name.  Returning absent makes the
    // enclosing call absent; returning a Value yields that value.
    Value apply(const FunctionCall& fc) noexcept;
};

}  // namespace arrow::efs
