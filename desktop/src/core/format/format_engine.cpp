// SPDX-License-Identifier: MPL-2.0
#include "core/format/format_engine.hpp"

#include <utility>

namespace arrow::format {

CompiledPattern compile(std::string_view source) noexcept {
    CompiledPattern out;
    out.source_.assign(source);
    ParseResult pr = parse(source);
    out.ast_ = std::move(pr.pattern);
    out.problems_ = std::move(pr.problems);
    return out;
}

RenderResult render(const CompiledPattern& pattern, const TrackView& track,
                    const EvalContext& ctx) noexcept {
    RenderResult out;
    out.pattern_length = pattern.source_.size();
    out.parse_problems = pattern.problems_;

    // Cap the output at the configured limit (REQ-EFS-009).  The
    // evaluator also stops internally at the same count, so the worst
    // case is bounded work regardless of how deeply a $repeat is
    // nested.  When the cap fires, we still return the truncated text
    // — the caller decides whether to surface the error or accept the
    // truncation, per REQ-EFS-006 ("renders as much as it can").
    EvalContext bounded = ctx;
    if (bounded.output_cap == 0) bounded.output_cap = kOutputCap;

    Evaluator ev{track, bounded};
    std::string text;
    text.reserve(256);
    bool cap = false;
    ev.evaluate(pattern.pattern(), text, &cap);
    out.text = std::move(text);
    out.cap_exceeded = cap;
    return out;
}

RenderResult render(std::string_view source, const TrackView& track,
                    const EvalContext& ctx) noexcept {
    return render(compile(source), track, ctx);
}

Error cap_error(std::size_t pattern_length) noexcept {
    Error e{ErrorCode::OutputCapExceeded,
            "Pattern output exceeded the 4096-character cap.",
            "pattern_length=" + std::to_string(pattern_length)};
    e.with_severity(Severity::Warning);
    return e;
}

}  // namespace arrow::format
