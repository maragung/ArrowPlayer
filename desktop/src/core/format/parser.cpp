// SPDX-License-Identifier: MPL-2.0
#include "core/format/parser.hpp"

#include <cctype>
#include <utility>

namespace arrow::efs {

namespace {

// Maximum nesting depth the parser allows for `[]` and `()`.  Conformance
// fixture `malformed-deep-nesting-terminates` documents 64 — the same limit
// the JSON parser uses, recorded as intentional, not a coincidence.
constexpr std::size_t kMaxNesting = 64;

// `identifier` per the EBNF: an ASCII letter, then ASCII letters/digits/
// underscore.  This is enough to cover the field names of REQ-EFS-007
// ("tracknumber", "length_seconds", "queue_index", …) and every function
// in REQ-EFS-008, and it keeps `$` from being a legal start so a stray
// `$5` cannot lex as `$5(...)`.
[[nodiscard]] bool is_ident_start(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
[[nodiscard]] bool is_ident_cont(char c) noexcept {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

// ---------------------------------------------------------------------------
//  Parser — single-pass recursive descent.  Holds the source by view, a
//  cursor, and a problem sink; everything else is on the stack of the
//  recursive call.
// ---------------------------------------------------------------------------

class Parser {
  public:
    explicit Parser(std::string_view src) : src_{src} {}

    [[nodiscard]] ParseResult run() noexcept {
        ParseResult out;
        out.pattern = parse_pattern(0, 0, &out.problems);
        return out;
    }

  private:
    std::string_view src_;
    std::size_t pos_{0};

    [[nodiscard]] bool eof() const noexcept { return pos_ >= src_.size(); }
    [[nodiscard]] char peek() const noexcept {
        return pos_ < src_.size() ? src_[pos_] : '\0';
    }
    [[nodiscard]] char take() noexcept {
        return pos_ < src_.size() ? src_[pos_++] : '\0';
    }

    void emit(std::vector<ParseProblem>* sink, ParseProblem::Code code,
              std::size_t start, std::size_t end) noexcept {
        sink->push_back(ParseProblem{code, start, end});
    }

    // ---------------------------------------------------------------- pattern
    //
    //  pattern = { element }
    //
    //  Reads until depth returns to caller or end of source.  `depth_open`
    //  is a counter of the outermost `()` / `[]` still open above us; the
    //  parser stops when the matching closer shows up.

    Pattern parse_pattern(std::size_t depth_open,
                          std::size_t bracket_depth,
                          std::vector<ParseProblem>* problems) noexcept {
        Pattern out;
        while (!eof()) {
            const char c = peek();
            if (depth_open > 0 && (c == ')' || c == ']')) break;
            if (c == '\'') {
                out.push_back(parse_quoted_run(problems));
            } else if (c == '%') {
                out.push_back(parse_field_ref(problems));
            } else if (c == '$') {
                out.push_back(parse_function(problems));
            } else if (c == '[') {
                parse_optional_block(depth_open, problems, &out);
            } else if (c == ']' && bracket_depth > 0) {
                // Only treat ']' as unmatched if we're inside a block.
                const std::size_t at = pos_;
                (void)take();
                emit(problems, ParseProblem::Code::UnmatchedCloseBracket, at, at + 1);
                LiteralText lit;
                lit.text.push_back(']');
                out.emplace_back(std::move(lit));
            } else if (c == ']') {
                // At top level, ']' is just a literal
                out.push_back(parse_literal_char());
            } else {
                // Plain literal character: not one of the four sigils.
                out.push_back(parse_literal_char());
            }
        }
        return out;
    }

    // ------------------------------------------------------------ literal char
    Node parse_literal_char() noexcept {
        const std::string s(1, take());
        return Node{LiteralText{s}};
    }

    // ------------------------------------------------------------ quoted run
    //
    //  "'" { any-char - "'" } "'"
    //  An unterminated run is closed at end of pattern (REQ-EFS-006).

    Node parse_quoted_run(std::vector<ParseProblem>* problems) noexcept {
        const std::size_t at = take();  // consume opening '
        std::string text;
        bool closed = false;
        while (!eof()) {
            const char c = take();
            if (c == '\'') {
                closed = true;
                break;
            }
            text.push_back(c);
        }
        if (!closed) {
            emit(problems, ParseProblem::Code::UnterminatedQuotedRun, at, pos_);
        }
        return Node{LiteralText{std::move(text)}};
    }

    // ------------------------------------------------------------ field ref
    //
    //  "%" identifier [ ":" format-spec ] "%"
    //
    //  Recovery on missing closer: keep the `%` and identifier as a literal
    //  so `by %artist` still prints `by %artist`.

    Node parse_field_ref(std::vector<ParseProblem>* problems) noexcept {
        const std::size_t at = take();  // consume '%'
        // "%" with no identifier after it — "%" or "%%" with another sigil.
        if (eof() || !is_ident_start(peek())) {
            LiteralText lit;
            lit.text.push_back('%');
            if (!eof() && peek() == '%') {
                lit.text.push_back(take());
                emit(problems, ParseProblem::Code::EmptyFieldRef, at, pos_);
            } else if (eof()) {
                emit(problems, ParseProblem::Code::UnterminatedFieldRef, at, pos_);
            } else {
                emit(problems, ParseProblem::Code::LonePercent, at, pos_);
            }
            return Node{std::move(lit)};
        }
        const std::size_t name_start = pos_;
        while (!eof() && is_ident_cont(peek())) ++pos_;
        std::string name{std::string_view{src_}.substr(name_start, pos_ - name_start)};

        FieldSpec spec{FieldSpec::None};
        int width{0};
        if (!eof() && peek() == ':') {
            (void)take();
            if (!eof() && peek() == 'u') {
                (void)take();
                spec = FieldSpec::Upper;
            } else if (!eof() && peek() == 'l') {
                (void)take();
                spec = FieldSpec::Lower;
            } else if (!eof() && peek() == 't') {
                (void)take();
                spec = FieldSpec::Title;
            } else {
                int w = 0;
                while (!eof() && peek() >= '0' && peek() <= '9') {
                    w = w * 10 + (take() - '0');
                    if (w > 99) w = 99;  // clamp so a runaway can't burn width
                }
                if (w > 0) {
                    spec = FieldSpec::ZeroPad;
                    width = w;
                }
            }
        }

        if (eof() || peek() != '%') {
            // Unterminated: keep `%name[:spec]` as a literal so the remainder
            // still renders.  malformed-unterminated-field-after-text confirms
            // the required behaviour.
            emit(problems, ParseProblem::Code::UnterminatedFieldRef, at, pos_);
            LiteralText lit;
            lit.text.append(src_.substr(at, pos_ - at));
            return Node{std::move(lit)};
        }
        (void)take();  // consume closing '%'

        FieldRef fr;
        fr.name = std::move(name);
        fr.spec = spec;
        fr.width = width;
        fr.start = at;
        fr.end = pos_;
        return Node{std::move(fr)};
    }

    // -------------------------------------------------------------- function
    //
    //  "$" identifier "(" [ arg { "," arg } ] ")"
    //
    //  When the function is unknown or the arity doesn't match a known
    //  signature, the whole call renders literally (REQ-EFS-006 /
    //  malformed-wrong-arity, malformed-unknown-function).

    Node parse_function(std::vector<ParseProblem>* problems) noexcept {
        const std::size_t at = take();  // consume '$'
        if (eof() || !is_ident_start(peek())) {
            // Stray '$' — render literally.
            emit(problems, ParseProblem::Code::LoneDollar, at, pos_);
            LiteralText lit;
            lit.text.push_back('$');
            return Node{std::move(lit)};
        }
        const std::size_t name_start = pos_;
        while (!eof() && is_ident_cont(peek())) ++pos_;
        const std::string name{std::string_view{src_}.substr(name_start, pos_ - name_start)};

        if (eof() || peek() != '(') {
            // "$name" with no parens — the spec only defines $name(...) form;
            // treat the whole as a literal so the user sees their typo.
            emit(problems, ParseProblem::Code::UnterminatedFunction, at, pos_);
            LiteralText lit;
            lit.text.append(src_.substr(at, pos_ - at));
            return Node{std::move(lit)};
        }
        (void)take();  // consume '('

        FunctionCall fc;
        fc.name = name;
        const std::size_t args_start = pos_;
        if (!eof() && peek() != ')') {
            fc.args.push_back(parse_arg(0, problems));
            while (!eof() && peek() == ',') {
                (void)take();
                fc.args.push_back(parse_arg(0, problems));
            }
        }
        const std::size_t args_end = pos_;

        if (eof() || peek() != ')') {
            // Unterminated: render literally.
            emit(problems, ParseProblem::Code::UnterminatedFunction, at, pos_);
            LiteralText lit;
            lit.text.append(src_.substr(at, pos_ - at));
            return Node{std::move(lit)};
        }
        (void)take();  // consume ')'

        // Validate the function name and arity now, so the engine can rely on
        // "every FunctionCall is well-formed" — but render the call as a
        // literal either way, never silently change the author's text.
        if (!is_known_function(name)) {
            emit(problems, ParseProblem::Code::UnknownFunction, at, pos_);
            LiteralText lit;
            lit.text.append(src_.substr(at, pos_ - at));
            return Node{std::move(lit)};
        }
        if (!is_valid_arity(name, fc.args.size())) {
            emit(problems, ParseProblem::Code::WrongArity, at, pos_);
            LiteralText lit;
            lit.text.append(src_.substr(at, pos_ - at));
            return Node{std::move(lit)};
        }

        fc.start = at;
        fc.end = pos_;
        (void)args_start;
        (void)args_end;
        return Node{std::move(fc)};
    }

    // ------------------------------------------------------------------- arg
    //
    //  arg = pattern     — until the matching ')' or ',' closes it.

    std::vector<Node> parse_arg(std::size_t parent_open,
                                std::vector<ParseProblem>* problems) noexcept {
        if (parent_open + 1 > kMaxNesting) {
            emit(problems, ParseProblem::Code::NestingTooDeep, pos_, pos_);
            return {};
        }
        return parse_pattern(parent_open + 1, true, problems);
    }

    // --------------------------------------------------------- optional block
    //
    //  "[" pattern "]"
    //
    //  An unclosed '[' renders as a literal bracket and the body still
    //  evaluates.  malformed-unclosed-block is the contract.

    void parse_optional_block(std::size_t parent_open,
                              std::vector<ParseProblem>* problems,
                              Pattern* out) noexcept {
        if (parent_open + 1 > kMaxNesting) {
            emit(problems, ParseProblem::Code::NestingTooDeep, pos_, pos_);
            (void)take();  // consume the '['
            LiteralText lit;
            lit.text.push_back('[');
            out->emplace_back(std::move(lit));
            return;
        }
        const std::size_t at = take();  // consume '['
        OptionalBlock blk;
        blk.body = parse_pattern(parent_open + 1, true, problems);
        if (eof() || peek() != ']') {
            // Unclosed '[' — bracket becomes a literal, the body is still
            // wrapped in a block (so its own collapse rule applies).  The
            // literal is appended FIRST so the surface always contains the
            // open bracket, no matter what the block does next.
            emit(problems, ParseProblem::Code::UnterminatedBlock, at, pos_);
            LiteralText lit;
            lit.text.push_back('[');
            out->emplace_back(std::move(lit));
            blk.start = at;
            blk.end = pos_;
            out->emplace_back(std::move(blk));
            return;
        }
        (void)take();  // consume ']'
        blk.start = at;
        blk.end = pos_;
        out->emplace_back(std::move(blk));
    }

    // ------------------------------------------------------ known functions
    //
    //  This list is the closed set REQ-EFS-008 names.  The evaluator looks
    //  the same names up at apply time; the parser's only job is to refuse
    //  the typo.  Aritiy is given as a half-open range [min, max]; the
    //  evaluator may further restrict, but the parser is generous so e.g.
    //  `$add(1,2,3,4,5)` (a five-arg sum) still parses and only the
    //  evaluator complains if it ever needs to.

    [[nodiscard]] static bool is_known_function(std::string_view name) noexcept {
        // Keep this list in sync with the dispatcher in evaluator.cpp.
        switch (name.size()) {
            case 2:
                if (name == "if") return true;
                break;
            case 3:
                if (name == "if2" || name == "if3" || name == "add" || name == "min" ||
                    name == "max" || name == "sub" || name == "mul" || name == "div" ||
                    name == "mod" || name == "abs" || name == "num" || name == "tab")
                    return true;
                break;
            case 4:
                if (name == "cut" || name == "sub2" || name == "left" || name == "pad" ||
                    name == "age" || name == "date" || name == "year" || name == "char" ||
                    name == "crlf" || name == "time")
                    return true;
                break;
            case 5:
                if (name == "upper" || name == "lower" || name == "title" || name == "trim" ||
                    name == "len" || name == "abbr" || name == "round" || name == "stars" ||
                    name == "fixed" || name == "right")
                    return true;
                break;
            case 6:
                if (name == "padright") return true;
                break;
            case 7:
                if (name == "insert" || name == "repeat" || name == "strchr" ||
                    name == "strstr" || name == "timems" || name == "ifgreater" ||
                    name == "iflonger")
                    return true;
                break;
            case 8:
                if (name == "ifequal" || name == "replace" || name == "meta_sep" ||
                    name == "ifless")
                    return true;
                break;
            case 9:
                if (name == "progress") return true;
                break;
            case 10:
                if (name == "caps") return true;
                break;
        }
        return false;
    }

    [[nodiscard]] static bool is_valid_arity(std::string_view name,
                                              std::size_t n) noexcept {
        // Exact arity per §10.5 / conformance.  Wrong arity makes the
        // call render literally (REQ-EFS-006 / OQ-010), so this table
        // is the contract: a stricter entry here makes more patterns
        // show their typo.
        if (name == "if") return n == 2 || n == 3;
        if (name == "if2") return n == 2;
        if (name == "if3") return n >= 2;
        if (name == "ifequal" || name == "ifgreater" || name == "ifless" ||
            name == "iflonger")
            return n == 4;
        if (name == "crlf" || name == "tab") return n == 0;
        if (name == "char" || name == "upper" || name == "lower" || name == "title" ||
            name == "trim" || name == "len" || name == "caps" || name == "abs" ||
            name == "year" || name == "age" || name == "time" || name == "timems" ||
            name == "sub2")
            return n == 1;
        if (name == "cut" || name == "right" || name == "left" || name == "div" ||
            name == "mod" || name == "repeat" || name == "fixed" || name == "num" ||
            name == "strchr" || name == "strstr" || name == "replace" ||
            name == "insert")
            return n == 2;
        if (name == "round" || name == "date" || name == "abbr" ||
            name == "meta_sep" || name == "stars")
            return n == 1 || n == 2;
        if (name == "pad" || name == "padright" || name == "sub")
            return n == 2 || n == 3;
        if (name == "progress") return n == 3 || n == 4 || n == 5;
        if (name == "add" || name == "mul") return n >= 2;
        if (name == "min" || name == "max") return n >= 1;
        return false;
    }
};

}  // namespace

ParseResult parse(std::string_view source) noexcept {
    Parser p{source};
    return p.run();
}

}  // namespace arrow::efs
