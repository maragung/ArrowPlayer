// SPDX-License-Identifier: MPL-2.0
// Tests for core/json — spec §21.2 (parser hardening), §11.2, REQ-SEC-002.
//
// Every JSON document Eclipse parses is untrusted (a downloaded skin, an
// imported settings bundle), so the limits are tested as hard guarantees.

#include <string>

#include "core/json/json.hpp"

#include <gtest/gtest.h>

using namespace eclipse;
using namespace eclipse::json;

namespace {
Value must_parse(std::string_view s, const Limits& lim = {}) {
    auto r = parse(s, lim);
    EXPECT_TRUE(r.has_value()) << (r.has_value() ? "" : r.error().to_log_string());
    return r.has_value() ? std::move(r).value() : Value{};
}
}  // namespace

// ===========================================================================
//  Well-formed input
// ===========================================================================

TEST(Json, ParsesScalars) {
    EXPECT_TRUE(must_parse("null").is_null());
    EXPECT_TRUE(must_parse("true").as_bool());
    EXPECT_FALSE(must_parse("false").as_bool());
    EXPECT_DOUBLE_EQ(must_parse("42").as_double(), 42.0);
    EXPECT_DOUBLE_EQ(must_parse("-3.5").as_double(), -3.5);
    EXPECT_DOUBLE_EQ(must_parse("1e3").as_double(), 1000.0);
    EXPECT_DOUBLE_EQ(must_parse("1.5E-2").as_double(), 0.015);
    EXPECT_EQ(must_parse("\"hi\"").as_string(), "hi");
}

TEST(Json, ParsesObjectsAndArrays) {
    const auto v = must_parse(R"({"a":1,"b":[true,null,"x"],"c":{"d":2}})");
    ASSERT_TRUE(v.is_object());
    ASSERT_NE(v.find("a"), nullptr);
    EXPECT_EQ(v.find("a")->as_int(), 1);

    const auto* b = v.find("b");
    ASSERT_NE(b, nullptr);
    ASSERT_TRUE(b->is_array());
    EXPECT_EQ(b->size(), 3u);
    EXPECT_TRUE(b->at(0)->as_bool());
    EXPECT_TRUE(b->at(1)->is_null());
    EXPECT_EQ(b->at(2)->as_string(), "x");

    EXPECT_EQ(v.find("c")->find("d")->as_int(), 2);
    EXPECT_EQ(v.at(0), nullptr);  // not an array
    EXPECT_EQ(v.find("missing"), nullptr);
}

TEST(Json, HandlesEmptyContainers) {
    EXPECT_EQ(must_parse("{}").size(), 0u);
    EXPECT_EQ(must_parse("[]").size(), 0u);
    EXPECT_TRUE(must_parse("{}").is_object());
    EXPECT_TRUE(must_parse("[]").is_array());
}

TEST(Json, ParsesStringEscapes) {
    // Plain (non-raw) literals: MSVC processes universal-character-names and
    // \" even inside raw strings, so the JSON text must be written with the
    // backslashes doubled instead of relying on raw-string passthrough.
    EXPECT_EQ(must_parse("\"a\\nb\"").as_string(), "a\nb");
    EXPECT_EQ(must_parse("\"q\\\"q\"").as_string(), "q\"q");
    EXPECT_EQ(must_parse("\"back\\\\slash\"").as_string(), "back\\slash");
    EXPECT_EQ(must_parse("\"tab\\there\"").as_string(), "tab\there");
    EXPECT_EQ(must_parse("\"\\u00e9\"").as_string(), "\u00E9");
    EXPECT_EQ(must_parse("\"\\u20AC\"").as_string(), "\u20AC");
}

TEST(Json, ParsesSurrogatePairs) {
    // U+1F600 as a UTF-16 surrogate pair must become 4-byte UTF-8. The JSON
    // text is "\ud83d\ude00" — written with doubled backslashes so no UCN is
    // formed in the C++ source (surrogates are ill-formed UCNs, C3850 on MSVC).
    EXPECT_EQ(must_parse("\"\\ud83d\\ude00\"").as_string(), "\U0001F600");
}

TEST(Json, ReplacesUnpairedSurrogates) {
    // Must not produce invalid UTF-8, and must not fail outright.
    EXPECT_EQ(must_parse("\"\\ud83d\"").as_string(), "\uFFFD");
    EXPECT_EQ(must_parse("\"\\udc00\"").as_string(), "\uFFFD");
}

TEST(Json, AcceptsJsoncCommentsAndTrailingCommas) {
    const auto v = must_parse(R"({
        // line comment
        "a": 1,  /* block comment */
        "b": 2,
    })");
    EXPECT_EQ(v.find("a")->as_int(), 1);
    EXPECT_EQ(v.find("b")->as_int(), 2);
}

TEST(Json, RejectsCommentsWhenDisabled) {
    Limits lim;
    lim.allow_comments = false;
    EXPECT_FALSE(parse("{\"a\":1} // x", lim).has_value());
}

TEST(Json, SkipsLeadingBom) {
    EXPECT_TRUE(must_parse("\xEF\xBB\xBF{\"a\":1}").is_object());
}

TEST(Json, IsIntegerDistinguishesWholeNumbers) {
    EXPECT_TRUE(must_parse("42").is_integer());
    EXPECT_TRUE(must_parse("-7").is_integer());
    EXPECT_TRUE(must_parse("1e3").is_integer());
    EXPECT_FALSE(must_parse("1.5").is_integer());
    EXPECT_FALSE(must_parse("\"5\"").is_integer());
}

// ===========================================================================
//  Malformed input — must fail cleanly, never crash or hang
// ===========================================================================

TEST(Json, RejectsMalformedDocuments) {
    const char* bad[] = {
        "",
        "{",
        "}",
        "[",
        "]",
        "{\"a\"}",
        "{\"a\":}",
        "{:1}",
        "{\"a\":1,}",  // handled, but "{,}" is not
        "{,}",
        "[,]",
        "[1,]",  // trailing comma cases vary
        "\"unterminated",
        "\"bad\\escape\"",
        "01",  // handled by trailing-content check
        "tru",
        "fals",
        "nul",
        "{\"a\":1}{\"b\":2}",  // content after the value
        "[1 2]",
        "{\"a\" 1}",
        "\"\\u00\"",  // truncated escape
        "\"\\uZZZZ\"",
        "+",
        "-",
        ".",
        "e5",
    };
    for (const char* s : bad) {
        auto r = parse(s);
        if (r.has_value()) {
            // A few of the above are legal under our JSONC relaxations; the
            // ones that are not must all be rejected.
            const std::string in{s};
            EXPECT_TRUE(in == "{\"a\":1,}" || in == "[1,]")
                << "should have been rejected: " << in;
        }
    }
}

TEST(Json, RejectsRawControlCharactersInStrings) {
    const std::string s =
        "\"a\x01"
        "b\"";
    auto r = parse(s);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::UnexpectedToken);
}

TEST(Json, ReportsDuplicateKeysRatherThanOverwriting) {
    // A duplicate in a theme file is an authoring bug the author must see.
    auto r = parse(R"({"a":1,"a":2})");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::SchemaViolation);
}

TEST(Json, RejectsInvalidUtf8InStrings) {
    // RFC 8259 §8.1: JSON text MUST be UTF-8. The parser must not accept bytes
    // it cannot represent losslessly: dump() re-encodes every string, so a raw
    // malformed byte would round-trip as U+FFFD and two distinct keys could
    // collapse into one. Found by fuzz_json (REQ-SEC-011): a document with
    // keys holding invalid bytes parsed fine, then dump()/parse() failed with
    // a duplicate-key error. Rejecting at parse time keeps the dump() round
    // trip a true invariant.
    const std::string bad =
        std::string{"{\"a"} + "\x8e\x8e" + "\":1,\"a" + "\xff\xff" + "\":2}";
    auto r = parse(bad);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::UnexpectedToken);

    // A lone continuation byte in a string is equally rejected.
    EXPECT_FALSE(parse(std::string{"\"a\x80"
                                   "b\""})
                     .has_value());
    // Valid multibyte UTF-8 must still be accepted.
    EXPECT_TRUE(parse("\"Bj\\u00f6rk\"").has_value());
    EXPECT_TRUE(parse(std::string{"\"caf\xc3"
                                  "\xa9\""})
                    .has_value());
}

TEST(Json, ErrorsCarryLineAndColumn) {
    auto r = parse("{\n  \"a\": 1,\n  \"b\": @\n}");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().line(), 3u);
    EXPECT_GT(r.error().column(), 0u);
}

// ===========================================================================
//  Hard limits — REQ-SEC-002, REQ-THM-017
// ===========================================================================

TEST(Json, EnforcesMaxBytes) {
    Limits lim;
    lim.max_bytes = 16;
    auto r = parse(std::string("\"") + std::string(100, 'a') + "\"", lim);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::InputTooLarge);
}

TEST(Json, EnforcesMaxDepth) {
    Limits lim;
    lim.max_depth = 20;
    std::string deep;
    for (int i = 0; i < 100; ++i) deep += '[';
    for (int i = 0; i < 100; ++i) deep += ']';

    auto r = parse(deep, lim);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::NestingTooDeep);
}

TEST(Json, DeepNestingDoesNotOverflowTheStack) {
    // 10,000 levels would smash the stack without the depth guard. With the
    // guard it must return an error promptly.
    std::string deep;
    for (int i = 0; i < 10000; ++i) deep += '[';
    auto r = parse(deep);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::NestingTooDeep);
}

TEST(Json, EnforcesMaxElements) {
    Limits lim;
    lim.max_elements = 50;
    std::string arr = "[";
    for (int i = 0; i < 500; ++i) {
        if (i) arr += ',';
        arr += '1';
    }
    arr += ']';

    auto r = parse(arr, lim);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code(), ErrorCode::InputTooLarge);
}

// ===========================================================================
//  JSON Pointer — used so validation errors can name the offending node
// ===========================================================================

TEST(Json, PointerLookup) {
    const auto v = must_parse(R"({"color":{"text":{"primary":"#fff"}},"arr":[10,20]})");
    ASSERT_NE(v.pointer("/color/text/primary"), nullptr);
    EXPECT_EQ(v.pointer("/color/text/primary")->as_string(), "#fff");
    EXPECT_EQ(v.pointer("/arr/1")->as_int(), 20);
    EXPECT_EQ(v.pointer(""), &v);
    EXPECT_EQ(v.pointer("/nope"), nullptr);
    EXPECT_EQ(v.pointer("/arr/9"), nullptr);
    EXPECT_EQ(v.pointer("no-leading-slash"), nullptr);
}

TEST(Json, PointerHandlesRfc6901Escapes) {
    const auto v = must_parse(R"({"a/b":1,"c~d":2})");
    ASSERT_NE(v.pointer("/a~1b"), nullptr);
    EXPECT_EQ(v.pointer("/a~1b")->as_int(), 1);
    ASSERT_NE(v.pointer("/c~0d"), nullptr);
    EXPECT_EQ(v.pointer("/c~0d")->as_int(), 2);
}

// ===========================================================================
//  Serialisation round-trip
// ===========================================================================

TEST(Json, DumpRoundTrips) {
    const std::string src = R"({"a":1,"b":[true,null,"x"],"c":{"d":-2.5}})";
    const auto v1 = must_parse(src);
    const auto v2 = must_parse(v1.dump());
    EXPECT_EQ(v1.dump(), v2.dump());
}

TEST(Json, DumpEscapesCorrectly) {
    const auto v = must_parse(R"({"k":"line\nbreak \"quoted\" \\ back"})");
    const auto out = v.dump();
    const auto again = must_parse(out);
    EXPECT_EQ(again.find("k")->as_string(), "line\nbreak \"quoted\" \\ back");
}

TEST(Json, DumpPrettyPrints) {
    const auto v = must_parse(R"({"a":1})");
    const auto pretty = v.dump(2);
    EXPECT_NE(pretty.find('\n'), std::string::npos);
    EXPECT_EQ(must_parse(pretty).find("a")->as_int(), 1);
}

TEST(Json, DumpPreservesUnicode) {
    // Same rationale as ParsesStringEscapes: doubled backslashes keep the JSON
    // text literal instead of letting MSVC form UCNs inside the raw string.
    const auto v = must_parse("{\"k\":\"Bj\\u00f6rk \\ud83c\\udfb5\"}");
    const auto again = must_parse(v.dump());
    EXPECT_EQ(again.find("k")->as_string(), "Bj\u00F6rk \U0001F3B5");
}

TEST(Json, ValueIsCopyable) {
    const auto v = must_parse(R"({"a":[1,2,3]})");
    const Value copy = v;  // deep copy
    EXPECT_EQ(copy.dump(), v.dump());
    EXPECT_EQ(copy.find("a")->size(), 3u);
}
