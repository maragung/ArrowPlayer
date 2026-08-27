// SPDX-License-Identifier: MPL-2.0
// Minimal, hardened JSON — spec §11.2 (theme schema), §11.4 (layout DSL),
// §19.4 (settings export), §21.2 (parser hardening).
//
// Why not a third-party library: the domain layer must link against nothing but
// the standard library (REQ-GEN-050). More importantly, every JSON document we
// parse is untrusted (a downloaded skin, an imported settings bundle), so the
// parser needs hard, auditable limits that we control:
//
//   * bounded nesting depth              (REQ-THM-017 / ErrorCode::NestingTooDeep)
//   * bounded document size              (ErrorCode::InputTooLarge)
//   * bounded member/element counts
//   * no duplicate-key silent overwrite  (reported, not ignored)
//   * exact position on every error, so the skin editor can point at the node
//
// This parser is a fuzz target: tests/fuzz/fuzz_theme.cpp (REQ-SEC-011).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace eclipse::json {

/// Parser limits. Defaults are the values the spec mandates for skin packages;
/// callers handling larger trusted documents may raise them explicitly.
struct Limits {
    std::size_t max_bytes = 8u * 1024u * 1024u;  ///< 8 MiB (REQ-THM-017)
    std::size_t max_depth = 64;                  ///< nesting guard
    std::size_t max_elements = 200'000;          ///< total nodes
    bool allow_comments = true;                  ///< // and /* */ (JSONC)
    bool allow_trailing_commas = true;
};

enum class Type { Null, Bool, Number, String, Array, Object };

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

/// An immutable-after-parse JSON value.
class Value {
  public:
    Value() noexcept : type_{Type::Null} {}

    explicit Value(bool b) noexcept : type_{Type::Bool}, bool_{b} {}

    explicit Value(double n) noexcept : type_{Type::Number}, num_{n} {}

    explicit Value(std::string s) : type_{Type::String}, str_{std::move(s)} {}

    explicit Value(Array a) : type_{Type::Array}, arr_{std::make_unique<Array>(std::move(a))} {}

    explicit Value(Object o)
          : type_{Type::Object}, obj_{std::make_unique<Object>(std::move(o))} {}

    Value(const Value& other) { copy_from(other); }

    Value& operator=(const Value& other) {
        if (this != &other) {
            reset();
            copy_from(other);
        }
        return *this;
    }

    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    [[nodiscard]] Type type() const noexcept { return type_; }

    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }

    [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::Bool; }

    [[nodiscard]] bool is_number() const noexcept { return type_ == Type::Number; }

    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }

    [[nodiscard]] bool is_array() const noexcept { return type_ == Type::Array; }

    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Object; }

    /// True when the number has no fractional part and fits in int64.
    [[nodiscard]] bool is_integer() const noexcept;

    [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
    [[nodiscard]] double as_double(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::int64_t as_int(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] std::string_view as_string(std::string_view fallback = {}) const noexcept;

    [[nodiscard]] const Array& as_array() const;
    [[nodiscard]] const Object& as_object() const;

    /// Object member lookup; returns nullptr when absent or not an object.
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;

    /// Array element; returns nullptr when out of range or not an array.
    [[nodiscard]] const Value* at(std::size_t index) const noexcept;

    /// RFC 6901 JSON Pointer lookup (e.g. "/color/text/primary"). Used so
    /// validation errors can name the exact offending node (REQ-THM-060).
    [[nodiscard]] const Value* pointer(std::string_view json_pointer) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    /// Serialises back to JSON. `indent == 0` produces compact output.
    [[nodiscard]] std::string dump(int indent = 0) const;

  private:
    void reset() noexcept;
    void copy_from(const Value& other);

    Type type_{Type::Null};
    bool bool_{false};
    double num_{0.0};
    std::string str_;
    std::unique_ptr<Array> arr_;
    std::unique_ptr<Object> obj_;
};

/// Parses `input`. On failure the Error carries line/column via Error::at().
[[nodiscard]] Result<Value> parse(std::string_view input, const Limits& limits = {});

/// Escapes a string as a JSON string literal, including the surrounding quotes.
[[nodiscard]] std::string escape(std::string_view s);

}  // namespace eclipse::json
