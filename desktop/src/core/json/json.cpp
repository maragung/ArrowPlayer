// SPDX-License-Identifier: MPL-2.0
#include "core/json/json.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include "core/text.hpp"

namespace arrow::json {

// ===========================================================================
//  Value
// ===========================================================================

void Value::reset() noexcept {
    arr_.reset();
    obj_.reset();
    str_.clear();
    type_ = Type::Null;
}

void Value::copy_from(const Value& other) {
    type_ = other.type_;
    bool_ = other.bool_;
    num_ = other.num_;
    str_ = other.str_;
    arr_ = other.arr_ ? std::make_unique<Array>(*other.arr_) : nullptr;
    obj_ = other.obj_ ? std::make_unique<Object>(*other.obj_) : nullptr;
}

bool Value::is_integer() const noexcept {
    if (type_ != Type::Number) return false;
    if (!std::isfinite(num_)) return false;
    if (num_ < -9.2233720368547758e18 || num_ > 9.2233720368547758e18) return false;
    double int_part = 0.0;
    return std::modf(num_, &int_part) == 0.0;
}

bool Value::as_bool(bool fallback) const noexcept {
    return type_ == Type::Bool ? bool_ : fallback;
}

double Value::as_double(double fallback) const noexcept {
    return type_ == Type::Number ? num_ : fallback;
}

std::int64_t Value::as_int(std::int64_t fallback) const noexcept {
    if (type_ != Type::Number || !std::isfinite(num_)) return fallback;
    if (num_ < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        num_ > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return fallback;
    return static_cast<std::int64_t>(num_);
}

std::string_view Value::as_string(std::string_view fallback) const noexcept {
    return type_ == Type::String ? std::string_view{str_} : fallback;
}

const Array& Value::as_array() const {
    static const Array kEmpty;
    return (type_ == Type::Array && arr_) ? *arr_ : kEmpty;
}

const Object& Value::as_object() const {
    static const Object kEmpty;
    return (type_ == Type::Object && obj_) ? *obj_ : kEmpty;
}

const Value* Value::find(std::string_view key) const noexcept {
    if (type_ != Type::Object || !obj_) return nullptr;
    const auto it = obj_->find(key);
    return it == obj_->end() ? nullptr : &it->second;
}

const Value* Value::at(std::size_t index) const noexcept {
    if (type_ != Type::Array || !arr_ || index >= arr_->size()) return nullptr;
    return &(*arr_)[index];
}

std::size_t Value::size() const noexcept {
    if (type_ == Type::Array && arr_) return arr_->size();
    if (type_ == Type::Object && obj_) return obj_->size();
    if (type_ == Type::String) return str_.size();
    return 0;
}

const Value* Value::pointer(std::string_view ptr) const noexcept {
    if (ptr.empty()) return this;
    if (ptr.front() != '/') return nullptr;

    const Value* cur = this;
    std::size_t i = 1;
    std::string token;
    while (cur != nullptr && i <= ptr.size()) {
        token.clear();
        while (i < ptr.size() && ptr[i] != '/') {
            // RFC 6901 escapes: ~1 -> '/', ~0 -> '~'
            if (ptr[i] == '~' && i + 1 < ptr.size()) {
                if (ptr[i + 1] == '1') {
                    token.push_back('/');
                    i += 2;
                    continue;
                } else if (ptr[i + 1] == '0') {
                    token.push_back('~');
                    i += 2;
                    continue;
                }
            }
            token.push_back(ptr[i]);
            ++i;
        }
        ++i;  // skip '/'

        if (cur->is_object()) {
            cur = cur->find(token);
        } else if (cur->is_array()) {
            std::int64_t idx = 0;
            if (!text::parse_int(token, idx) || idx < 0) return nullptr;
            cur = cur->at(static_cast<std::size_t>(idx));
        } else {
            return nullptr;
        }
    }
    return cur;
}

// ===========================================================================
//  Serialisation
// ===========================================================================

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    std::size_t pos = 0;
    while (pos < s.size()) {
        const char32_t cp = text::decode_utf8(s, pos);
        switch (cp) {
            case U'"':
                out += "\\\"";
                break;
            case U'\\':
                out += "\\\\";
                break;
            case U'\b':
                out += "\\b";
                break;
            case U'\f':
                out += "\\f";
                break;
            case U'\n':
                out += "\\n";
                break;
            case U'\r':
                out += "\\r";
                break;
            case U'\t':
                out += "\\t";
                break;
            default:
                if (cp < 0x20u) {
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(cp >> 4) & 0xF]);
                    out.push_back(kHex[cp & 0xF]);
                } else {
                    text::encode_utf8(cp, out);
                }
        }
    }
    out.push_back('"');
    return out;
}

namespace {

void format_number(double v, std::string& out) {
    if (!std::isfinite(v)) {
        out += "null";
        return;
    }  // JSON has no Inf/NaN
    double int_part = 0.0;
    if (std::modf(v, &int_part) == 0.0 && std::abs(v) < 1e15) {
        out += std::to_string(static_cast<std::int64_t>(v));
        return;
    }
    char buf[40];
    const int n = std::snprintf(buf, sizeof buf, "%.17g", v);
    if (n > 0) out.append(buf, static_cast<std::size_t>(n));
}

void dump_into(const Value& v, int indent, int level, std::string& out) {
    const auto newline_indent = [&](int lvl) {
        if (indent <= 0) return;
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(lvl), ' ');
    };

    switch (v.type()) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += v.as_bool() ? "true" : "false";
            break;
        case Type::Number:
            format_number(v.as_double(), out);
            break;
        case Type::String:
            out += escape(v.as_string());
            break;
        case Type::Array: {
            const auto& a = v.as_array();
            if (a.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i) out.push_back(',');
                newline_indent(level + 1);
                dump_into(a[i], indent, level + 1, out);
            }
            newline_indent(level);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const auto& o = v.as_object();
            if (o.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            bool first = true;
            for (const auto& [key, val] : o) {
                if (!first) out.push_back(',');
                first = false;
                newline_indent(level + 1);
                out += escape(key);
                out.push_back(':');
                if (indent > 0) out.push_back(' ');
                dump_into(val, indent, level + 1, out);
            }
            newline_indent(level);
            out.push_back('}');
            break;
        }
    }
}

}  // namespace

std::string Value::dump(int indent) const {
    std::string out;
    dump_into(*this, indent, 0, out);
    return out;
}

// ===========================================================================
//  Parser
// ===========================================================================

namespace {

class Parser {
  public:
    Parser(std::string_view input, const Limits& limits) : src_{input}, lim_{limits} {}

    Result<Value> run() {
        skip_trivia();
        auto root = parse_value(0);
        if (!root) return root;
        skip_trivia();
        if (pos_ != src_.size())
            return fail(ErrorCode::UnexpectedToken, "Unexpected content after the JSON value.");
        return root;
    }

  private:
    std::string_view src_;
    Limits lim_;
    std::size_t pos_{0};
    std::size_t line_{1};
    std::size_t col_{1};
    std::size_t elements_{0};

    [[nodiscard]] bool eof() const noexcept { return pos_ >= src_.size(); }

    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : src_[pos_]; }

    char advance() noexcept {
        if (eof()) return '\0';
        const char c = src_[pos_++];
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        return c;
    }

    [[nodiscard]] Error fail(ErrorCode code, std::string message) const {
        Error e{code, std::move(message)};
        e.at(pos_, line_, col_);
        return e;
    }

    void skip_trivia() noexcept {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
                continue;
            }
            // UTF-8 BOM, tolerated only at the very start.
            if (pos_ == 0 && src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
                static_cast<unsigned char>(src_[1]) == 0xBB &&
                static_cast<unsigned char>(src_[2]) == 0xBF) {
                advance();
                advance();
                advance();
                continue;
            }
            if (lim_.allow_comments && c == '/' && pos_ + 1 < src_.size()) {
                if (src_[pos_ + 1] == '/') {
                    while (!eof() && peek() != '\n') advance();
                    continue;
                }
                if (src_[pos_ + 1] == '*') {
                    advance();
                    advance();
                    while (!eof()) {
                        if (peek() == '*' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                            advance();
                            advance();
                            break;
                        }
                        advance();
                    }
                    continue;
                }
            }
            break;
        }
    }

    Result<Value> parse_value(std::size_t depth) {
        if (depth > lim_.max_depth)
            return fail(ErrorCode::NestingTooDeep, "The document is nested too deeply.");
        if (++elements_ > lim_.max_elements)
            return fail(ErrorCode::InputTooLarge, "The document contains too many values.");

        skip_trivia();
        if (eof()) return fail(ErrorCode::UnexpectedEnd, "Unexpected end of input.");

        switch (peek()) {
            case '{':
                return parse_object(depth);
            case '[':
                return parse_array(depth);
            case '"': {
                auto s = parse_string();
                if (!s) return std::move(s).error();
                return Value{std::move(s).value()};
            }
            case 't':
                return parse_literal("true", Value{true});
            case 'f':
                return parse_literal("false", Value{false});
            case 'n':
                return parse_literal("null", Value{});
            default:
                return parse_number();
        }
    }

    // By-value `result` is deliberate: the callers hand over fresh temporaries
    // (Value{true} etc.) and the parameter is moved out on return — the implicit
    // move in `return result;` is exactly that, and GCC's -Wredundant-move
    // rejects spelling it out. cppcheck's passedByValue (REQ-BLD-021) does not
    // model the implicit move, so the finding is suppressed here rather than
    // by weakening the parameter to const& (which would force a copy).
    // cppcheck-suppress passedByValue
    Result<Value> parse_literal(std::string_view word, Value result) {
        if (src_.compare(pos_, word.size(), word) != 0)
            return fail(ErrorCode::UnexpectedToken, "Invalid literal.");
        for (std::size_t i = 0; i < word.size(); ++i) advance();
        return result;
    }

    Result<Value> parse_object(std::size_t depth) {
        advance();  // '{'
        Object obj;
        skip_trivia();
        if (peek() == '}') {
            advance();
            return Value{std::move(obj)};
        }

        while (true) {
            skip_trivia();
            if (peek() != '"')
                return fail(ErrorCode::UnexpectedToken, "Expected a quoted member name.");
            auto key = parse_string();
            if (!key) return std::move(key).error();

            skip_trivia();
            if (peek() != ':')
                return fail(ErrorCode::UnexpectedToken, "Expected ':' after the member name.");
            advance();

            auto val = parse_value(depth + 1);
            if (!val) return val;

            // Duplicate keys are reported, never silently overwritten: in a theme
            // file a duplicate is an authoring bug the author needs to see.
            if (obj.find(key.value()) != obj.end())
                return fail(ErrorCode::SchemaViolation,
                            "Duplicate member name '" + key.value() + "'.");
            obj.emplace(std::move(key).value(), std::move(val).value());

            skip_trivia();
            if (peek() == ',') {
                advance();
                skip_trivia();
                if (peek() == '}') {
                    if (!lim_.allow_trailing_commas)
                        return fail(ErrorCode::UnexpectedToken, "Trailing comma.");
                    advance();
                    return Value{std::move(obj)};
                }
                continue;
            }
            if (peek() == '}') {
                advance();
                return Value{std::move(obj)};
            }
            return fail(ErrorCode::UnexpectedToken, "Expected ',' or '}'.");
        }
    }

    Result<Value> parse_array(std::size_t depth) {
        advance();  // '['
        Array arr;
        skip_trivia();
        if (peek() == ']') {
            advance();
            return Value{std::move(arr)};
        }

        while (true) {
            auto val = parse_value(depth + 1);
            if (!val) return val;
            arr.push_back(std::move(val).value());

            skip_trivia();
            if (peek() == ',') {
                advance();
                skip_trivia();
                if (peek() == ']') {
                    if (!lim_.allow_trailing_commas)
                        return fail(ErrorCode::UnexpectedToken, "Trailing comma.");
                    advance();
                    return Value{std::move(arr)};
                }
                continue;
            }
            if (peek() == ']') {
                advance();
                return Value{std::move(arr)};
            }
            return fail(ErrorCode::UnexpectedToken, "Expected ',' or ']'.");
        }
    }

    Result<std::string> parse_string() {
        advance();  // opening quote
        std::string out;
        while (true) {
            if (eof()) return fail(ErrorCode::UnexpectedEnd, "Unterminated string.");
            const char c = advance();
            if (c == '"') {
                // RFC 8259 §8.1: JSON text MUST be UTF-8. The parser must not
                // accept bytes it cannot represent: dump() re-encodes every
                // string through text::encode_utf8, so a raw malformed byte
                // would come back as U+FFFD and two distinct keys could
                // collapse into one — a round-trip failure the fuzzer's
                // dump()/parse() invariant exists to catch.
                if (!text::is_valid_utf8(out))
                    return fail(ErrorCode::UnexpectedToken, "String contains invalid UTF-8.");
                return out;
            }

            if (c != '\\') {
                // Raw control characters are invalid in a JSON string.
                if (static_cast<unsigned char>(c) < 0x20)
                    return fail(ErrorCode::UnexpectedToken,
                                "Unescaped control character in string.");
                out.push_back(c);
                continue;
            }

            if (eof()) return fail(ErrorCode::UnexpectedEnd, "Unterminated escape.");
            switch (advance()) {
                case '"':
                    out.push_back('"');
                    break;
                case '\\':
                    out.push_back('\\');
                    break;
                case '/':
                    out.push_back('/');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    auto cp = parse_hex4();
                    if (!cp) return std::move(cp).error();
                    char32_t code = cp.value();
                    // Surrogate pair.
                    if (code >= 0xD800u && code <= 0xDBFFu) {
                        if (pos_ + 1 < src_.size() && src_[pos_] == '\\' &&
                            src_[pos_ + 1] == 'u') {
                            advance();
                            advance();
                            auto low = parse_hex4();
                            if (!low) return std::move(low).error();
                            if (low.value() >= 0xDC00u && low.value() <= 0xDFFFu) {
                                code = 0x10000u + ((code - 0xD800u) << 10) +
                                       (low.value() - 0xDC00u);
                            } else {
                                code = 0xFFFDu;  // unpaired high surrogate
                            }
                        } else {
                            code = 0xFFFDu;
                        }
                    } else if (code >= 0xDC00u && code <= 0xDFFFu) {
                        code = 0xFFFDu;  // lone low surrogate
                    }
                    text::encode_utf8(code, out);
                    break;
                }
                default:
                    return fail(ErrorCode::UnexpectedToken, "Invalid escape sequence.");
            }
        }
    }

    Result<char32_t> parse_hex4() {
        if (pos_ + 4 > src_.size())
            return fail(ErrorCode::UnexpectedEnd, "Truncated \\u escape.");
        std::uint64_t v = 0;
        if (!text::parse_hex(src_.substr(pos_, 4), v))
            return fail(ErrorCode::UnexpectedToken, "Invalid \\u escape.");
        for (int i = 0; i < 4; ++i) advance();
        return static_cast<char32_t>(v);
    }

    Result<Value> parse_number() {
        const std::size_t start = pos_;

        // RFC 8259: number = [ minus ] int [ frac ] [ exp ]
        // A leading '+' is not permitted, and only "0" may start with zero.
        // Being strict here matters: this parser reads untrusted skin and
        // settings documents, and lax number parsing has been a source of
        // real-world divergence between implementations.
        if (peek() == '-') advance();

        if (eof() || peek() < '0' || peek() > '9')
            return fail(ErrorCode::UnexpectedToken, "Expected a JSON value.");

        if (peek() == '0') {
            advance();
            if (!eof() && peek() >= '0' && peek() <= '9')
                return fail(ErrorCode::UnexpectedToken, "Numbers must not have leading zeros.");
        } else {
            while (!eof() && peek() >= '0' && peek() <= '9') advance();
        }

        if (!eof() && peek() == '.') {
            advance();
            bool frac_digit = false;
            while (!eof() && peek() >= '0' && peek() <= '9') {
                advance();
                frac_digit = true;
            }
            if (!frac_digit)
                return fail(ErrorCode::UnexpectedToken,
                            "Expected a digit after the decimal point.");
        }

        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!eof() && (peek() == '+' || peek() == '-')) advance();
            bool exp_digit = false;
            while (!eof() && peek() >= '0' && peek() <= '9') {
                advance();
                exp_digit = true;
            }
            if (!exp_digit)
                return fail(ErrorCode::UnexpectedToken, "Malformed number exponent.");
        }

        double out = 0.0;
        if (!text::parse_double(src_.substr(start, pos_ - start), out))
            return fail(ErrorCode::OutOfRange, "Number is out of representable range.");
        return Value{out};
    }
};

}  // namespace

Result<Value> parse(std::string_view input, const Limits& limits) {
    if (input.size() > limits.max_bytes) {
        Error e{ErrorCode::InputTooLarge, "The document is too large."};
        return e.with_detail("size=" + std::to_string(input.size()) +
                             " limit=" + std::to_string(limits.max_bytes));
    }
    Parser p{input, limits};
    return p.run();
}

}  // namespace arrow::json
