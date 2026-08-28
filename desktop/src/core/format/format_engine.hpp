// SPDX-License-Identifier: MPL-2.0
// EFS engine — public entry point.  Spec §10, REQ-EFS-001 .. REQ-EFS-012.
//
// Two layers live here, deliberately small:
//
//   * CompiledPattern  — the parsed AST plus any parse problems.  Parsing
//     happens once and is reusable across many evaluations (a playlist
//     column re-renders the same pattern for every row).
//
//   * render()        — the single function a caller uses.  It evaluates
//     a CompiledPattern against a TrackView and returns the rendered
//     string, or an Error when the output cap would be exceeded
//     (REQ-EFS-009) or a parse problem should be reported to the editor
//     (REQ-EFS-011).
//
// Errors never throw.  A malformed pattern still produces a string
// (REQ-EFS-006); the editor surfaces the problems alongside the
// rendered text.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"
#include "core/format/evaluator.hpp"
#include "core/format/parser.hpp"

namespace arrow::format {

// ==================================================================== output

/// The hard output cap, in grapheme clusters.  REQ-EFS-009.
/// Two places enforce it: the engine refuses to return a longer string
/// (this constant), and the evaluator stops appending at this count
/// when rendering so a deeply nested $repeat cannot exhaust memory
/// before the cap is reached.
inline constexpr std::size_t kOutputCap = 4096;

/// REQ-EFS-009: the per-call cap on $repeat / $progress width.
inline constexpr std::size_t kRepeatCap = 256;

// ==================================================================== result

/// A render result.  `text` is always populated — the engine never
/// returns an empty success.  `cap_exceeded` is set when the rendered
/// string would have exceeded the output cap; `pattern_length` records
/// the source length so the editor can name the pattern.
struct RenderResult {
    std::string text;
    bool cap_exceeded{false};
    std::size_t pattern_length{0};
    std::vector<ParseProblem> parse_problems;
};

// ==================================================================== Compiled

/// A parsed, reusable pattern.  Constructed by compile(); evaluated
/// repeatedly by render().  Copyable so it can live in a column-spec
/// table or a skin bundle.
class CompiledPattern {
  public:
    CompiledPattern() = default;

    [[nodiscard]] const Pattern& pattern() const noexcept { return ast_; }
    [[nodiscard]] const std::vector<ParseProblem>& problems() const noexcept {
        return problems_;
    }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] bool malformed() const noexcept { return !problems_.empty(); }

  private:
    friend CompiledPattern compile(std::string_view source) noexcept;
    std::string source_;
    Pattern ast_;
    std::vector<ParseProblem> problems_;
};

/// Parse `source` once.  Never fails: a malformed pattern still yields
/// a CompiledPattern whose `problems()` is non-empty, and whose
/// `render()` call returns the literal remainder per REQ-EFS-006.
[[nodiscard]] CompiledPattern compile(std::string_view source) noexcept;

/// Render a compiled pattern against a track.  `ctx` carries locale
/// and "now" for $date / $age; defaults are en-US and the track's
/// `now_unix()`.
[[nodiscard]] RenderResult render(const CompiledPattern& pattern,
                                   const TrackView& track,
                                   const EvalContext& ctx = {}) noexcept;

/// Convenience: compile + render in one call.  Useful for the CLI demo
/// and for one-shot calls from the editor preview.
[[nodiscard]] RenderResult render(std::string_view source,
                                   const TrackView& track,
                                   const EvalContext& ctx = {}) noexcept;

/// Convenience: a Status that names the cap and the pattern length,
/// matching the wording REQ-EFS-009 requires of the user-facing
/// message.  `pattern_length` is the source-pattern byte count.
[[nodiscard]] Error cap_error(std::size_t pattern_length) noexcept;

}  // namespace arrow::format
