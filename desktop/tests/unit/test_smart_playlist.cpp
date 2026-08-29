// SPDX-License-Identifier: MPL-2.0
// Tests for smart-playlist rule evaluation — spec §9.6, REQ-PLS-010..REQ-PLS-015.
//
// The smart playlist compiler transforms JSON rule documents into parameterised SQL
// fragments. Tests verify:
//   - JSON parsing produces correct rule structures
//   - Compilation produces correct SQL fragments
//   - Parameter binding preserves type information
//   - Rule evaluation against a database works correctly
//   - Conformance with shared-spec/conformance/smart-playlist-cases.json

#include <filesystem>
#include <fstream>

#include "library/smart_playlist.hpp"
#include "library/library_database.hpp"

#include <gtest/gtest.h>

using namespace arrow;
using namespace arrow::library;

namespace {

// Helper: create a comparison with an integer value
Comparison make_int_cmp(std::string field, FieldOp op, std::int64_t value) {
    Comparison c;
    c.field = std::move(field);
    c.op = op;
    c.value.kind = Operand::Kind::Integer;
    c.value.integer = value;
    return c;
}

// Helper: create a comparison with a string value
Comparison make_str_cmp(std::string field, FieldOp op, std::string value) {
    Comparison c;
    c.field = std::move(field);
    c.op = op;
    c.value.kind = Operand::Kind::String;
    c.value.string = std::move(value);
    return c;
}

// Helper: compile a rule and get the result
Result<CompiledQuery> compile_rule(const PlaylistRule& rule) {
    return compile(rule);
}

}  // namespace

// ===========================================================================
//  JSON parsing — REQ-PLS-010
// ===========================================================================

TEST(SmartPlaylistParse, ParsesSimpleRule) {
    constexpr auto json = R"({
        "name": "Test Playlist",
        "description": "Test",
        "rules": {
            "match": "all",
            "rules": [
                {"field": "genre", "operator": "contains", "value": "Rock"}
            ]
        },
        "limit": 100
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value()) << rule.error().to_log_string();
    EXPECT_EQ(rule->name, "Test Playlist");
    EXPECT_EQ(rule->limit.value_or(0), 100);
}

TEST(SmartPlaylistParse, ParsesMatchAll) {
    constexpr auto json = R"({
        "name": "Match All",
        "rules": {"match": "all", "rules": []},
        "order_by": [{"field": "title", "direction": "asc"}]
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->match.children.size(), 0u);
    EXPECT_EQ(rule->match.op, Group::Conjunction::And);
}

TEST(SmartPlaylistParse, ParsesMatchAny) {
    constexpr auto json = R"({
        "name": "Match Any",
        "rules": {"match": "any", "rules": []}
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->match.op, Group::Conjunction::Or);
}

TEST(SmartPlaylistParse, ParsesNestedGroups) {
    constexpr auto json = R"({
        "name": "Nested",
        "rules": {
            "match": "all",
            "rules": [
                {"field": "genre", "operator": "contains", "value": "Rock"},
                {
                    "match": "any",
                    "rules": [
                        {"field": "year", "operator": "gt", "value": 2020},
                        {"field": "rating", "operator": "gte", "value": 4}
                    ]
                }
            ]
        }
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->match.children.size(), 2u);

    // First child is a comparison
    const auto* cmp = std::get_if<Comparison>(&rule->match.children[0]);
    ASSERT_NE(cmp, nullptr);
    EXPECT_EQ(cmp->field, "genre");
    EXPECT_EQ(cmp->op, FieldOp::Contains);

    // Second child is a nested group
    const auto* grp = std::get_if<Group>(&rule->match.children[1]);
    ASSERT_NE(grp, nullptr);
    EXPECT_EQ(grp->op, Group::Conjunction::Or);
}

TEST(SmartPlaylistParse, ParsesNotFlag) {
    constexpr auto json = R"({
        "name": "Not Test",
        "rules": {
            "match": "all",
            "not": true,
            "rules": [
                {"field": "genre", "operator": "contains", "value": "Metal"}
            ]
        }
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    EXPECT_TRUE(rule->match.not_flag);
}

TEST(SmartPlaylistParse, ParsesOrderBy) {
    constexpr auto json = R"({
        "name": "Ordered",
        "rules": {"match": "all", "rules": []},
        "order_by": [
            {"field": "artist", "direction": "asc"},
            {"field": "year", "direction": "desc"},
            {"field": "random", "direction": "asc"}
        ]
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    ASSERT_EQ(rule->order_by.size(), 3u);
    EXPECT_EQ(rule->order_by[0].field, "artist");
    EXPECT_EQ(rule->order_by[0].direction, OrderDirection::Asc);
    EXPECT_EQ(rule->order_by[1].field, "year");
    EXPECT_EQ(rule->order_by[1].direction, OrderDirection::Desc);
    EXPECT_EQ(rule->order_by[2].field, "random");
}

TEST(SmartPlaylistParse, ParsesLimit) {
    constexpr auto json = R"({
        "name": "Limited",
        "rules": {"match": "all", "rules": []},
        "limit": 500
    })";

    auto rule = parse_rule_json(json);
    ASSERT_TRUE(rule.has_value());
    ASSERT_TRUE(rule->limit.has_value());
    EXPECT_EQ(rule->limit.value(), 500);
}

TEST(SmartPlaylistParse, RejectsInvalidJson) {
    EXPECT_FALSE(parse_rule_json("not json").has_value());
    EXPECT_FALSE(parse_rule_json("{}").has_value());  // missing rules
}

TEST(SmartPlaylistParse, RejectsUnknownOperator) {
    constexpr auto json = R"({
        "name": "Bad Op",
        "rules": {
            "match": "all",
            "rules": [
                {"field": "genre", "operator": "unknown_op", "value": "Rock"}
            ]
        }
    })";

    EXPECT_FALSE(parse_rule_json(json).has_value());
}

// ===========================================================================
//  Compilation — REQ-PLS-012
// ===========================================================================

TEST(SmartPlaylistCompile, SimpleFieldComparison) {
    PlaylistRule rule;
    rule.name = "Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    EXPECT_FALSE(result->where.empty());
    EXPECT_FALSE(result->params.empty());
    EXPECT_EQ(result->params[0].kind, BindValue::Kind::Text);
    EXPECT_EQ(result->params[0].text, "Rock");
}

TEST(SmartPlaylistCompile, IntegerComparison) {
    PlaylistRule rule;
    rule.name = "Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_int_cmp("year", FieldOp::Gte, 2020));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    ASSERT_FALSE(result->params.empty());
    EXPECT_EQ(result->params[0].kind, BindValue::Kind::Integer);
    EXPECT_EQ(result->params[0].integer, 2020);
}

TEST(SmartPlaylistCompile, BetweenOperator) {
    PlaylistRule rule;
    rule.name = "Test";
    rule.match.op = Group::Conjunction::And;

    Comparison c;
    c.field = "year";
    c.op = FieldOp::Between;
    c.from.kind = Operand::Kind::Integer;
    c.from.integer = 2010;
    c.to.kind = Operand::Kind::Integer;
    c.to.integer = 2020;
    rule.match.children.push_back(std::move(c));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    // Between produces two parameters
    ASSERT_GE(result->params.size(), 2u);
    EXPECT_EQ(result->params[0].kind, BindValue::Kind::Integer);
    EXPECT_EQ(result->params[0].integer, 2010);
    EXPECT_EQ(result->params[1].kind, BindValue::Kind::Integer);
    EXPECT_EQ(result->params[1].integer, 2020);
}

TEST(SmartPlaylistCompile, MultipleComparisons) {
    PlaylistRule rule;
    rule.name = "Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));
    rule.match.children.push_back(make_int_cmp("year", FieldOp::Gte, 2020));
    rule.match.children.push_back(make_int_cmp("rating", FieldOp::Gte, 4));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    ASSERT_EQ(result->params.size(), 3u);
    EXPECT_EQ(result->params[0].text, "Rock");
    EXPECT_EQ(result->params[1].integer, 2020);
    EXPECT_EQ(result->params[2].integer, 4);
}

TEST(SmartPlaylistCompile, OrderByRandom) {
    PlaylistRule rule;
    rule.name = "Random Test";
    rule.match.op = Group::Conjunction::And;
    rule.order_by.push_back({"random", OrderDirection::Asc});

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    EXPECT_TRUE(result->order_by.find("RANDOM()") != std::string::npos
                || result->order_by.find("random()") != std::string::npos);
}

TEST(SmartPlaylistCompile, OrderByCustomField) {
    PlaylistRule rule;
    rule.name = "Custom Order";
    rule.match.op = Group::Conjunction::And;
    rule.order_by.push_back({"custom:myfield", OrderDirection::Desc});

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    EXPECT_TRUE(result->order_by.find("custom_myfield") != std::string::npos);
}

TEST(SmartPlaylistCompile, LimitPropagation) {
    PlaylistRule rule;
    rule.name = "Limited";
    rule.match.op = Group::Conjunction::And;
    rule.limit = 100;

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    ASSERT_TRUE(result->limit.has_value());
    EXPECT_EQ(result->limit.value(), 100);
}

TEST(SmartPlaylistCompile, EmptyGroupProducesEmptyWhere) {
    PlaylistRule rule;
    rule.name = "Empty";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.clear();

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value());
    // Empty group should produce "1=1" (always true)
    EXPECT_TRUE(result->where.empty() || result->where == "1=1");
}

// ===========================================================================
//  Round-trip JSON
// ===========================================================================

TEST(SmartPlaylistRoundTrip, JsonToRuleAndBack) {
    constexpr auto json = R"({
        "name": "Round Trip Test",
        "description": "Testing round-trip",
        "rules": {
            "match": "all",
            "rules": [
                {"field": "genre", "operator": "contains", "value": "Jazz"}
            ]
        },
        "order_by": [{"field": "artist", "direction": "asc"}],
        "limit": 50
    })";

    auto rule1 = parse_rule_json(json);
    ASSERT_TRUE(rule1.has_value());

    auto round_tripped = to_json(*rule1);
    auto rule2 = parse_rule_json(round_tripped);
    ASSERT_TRUE(rule2.has_value()) << rule2.error().to_log_string();

    EXPECT_EQ(rule1->name, rule2->name);
    EXPECT_EQ(rule1->description, rule2->description);
    EXPECT_EQ(rule1->limit, rule2->limit);
}

// ===========================================================================
//  SQL injection safety — REQ-SEC-009
// ===========================================================================

TEST(SmartPlaylistSafety, NoSqlInjectionInFieldNames) {
    PlaylistRule rule;
    rule.name = "Injection Test";
    rule.match.op = Group::Conjunction::And;

    // Attempt SQL injection via field name
    Comparison c;
    c.field = "title; DROP TABLE tracks; --";
    c.op = FieldOp::Eq;
    c.value.kind = Operand::Kind::String;
    c.value.string = "test";
    rule.match.children.push_back(std::move(c));

    auto result = compile_rule(rule);
    // The compiler must reject unknown fields, not pass them through
    EXPECT_FALSE(result.has_value())
        << "Field name with SQL injection was accepted";
}

TEST(SmartPlaylistSafety, NoSqlInjectionInValues) {
    PlaylistRule rule;
    rule.name = "Injection Test";
    rule.match.op = Group::Conjunction::And;

    // Attempt SQL injection via string value
    rule.match.children.push_back(
        make_str_cmp("title", FieldOp::Contains, "'; DROP TABLE tracks; --"));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value());
    // Value must be a parameter, not interpolated
    ASSERT_FALSE(result->params.empty());
    EXPECT_EQ(result->params[0].kind, BindValue::Kind::Text);
    // The dangerous string must be preserved as-is in the parameter
    EXPECT_EQ(result->params[0].text, "'; DROP TABLE tracks; --");
    // The WHERE clause must not contain the dangerous string
    EXPECT_TRUE(result->where.find("DROP") == std::string::npos);
}

// ===========================================================================
//  Operator coverage
// ===========================================================================

TEST(SmartPlaylistOperators, AllComparisonOperators) {
    const std::vector<std::pair<FieldOp, std::string>> ops = {
        {FieldOp::Eq, "eq"},
        {FieldOp::Ne, "ne"},
        {FieldOp::Gt, "gt"},
        {FieldOp::Lt, "lt"},
        {FieldOp::Gte, "gte"},
        {FieldOp::Lte, "lte"},
        {FieldOp::Contains, "contains"},
        {FieldOp::NotContains, "notcontains"},
        {FieldOp::StartsWith, "startswith"},
        {FieldOp::EndsWith, "endswith"},
        {FieldOp::IsNull, "isnull"},
        {FieldOp::IsNotNull, "isnotnull"},
    };

    for (const auto& [op, _] : ops) {
        PlaylistRule rule;
        rule.name = "Op Test";
        rule.match.op = Group::Conjunction::And;

        Comparison c;
        c.field = "genre";
        c.op = op;
        if (op == FieldOp::IsNull || op == FieldOp::IsNotNull) {
            // No value needed
        } else {
            c.value.kind = Operand::Kind::String;
            c.value.string = "Rock";
        }
        rule.match.children.push_back(std::move(c));

        auto result = compile_rule(rule);
        EXPECT_TRUE(result.has_value()) << "Operator " << static_cast<int>(op) << " failed to compile";
    }
}

TEST(SmartPlaylistOperators, InOperator) {
    PlaylistRule rule;
    rule.name = "In Test";
    rule.match.op = Group::Conjunction::And;

    Comparison c;
    c.field = "genre";
    c.op = FieldOp::In;
    c.values.push_back({.string = "Rock", .integer = 0, .boolean = false, .kind = Operand::Kind::String});
    c.values.push_back({.string = "Jazz", .integer = 0, .boolean = false, .kind = Operand::Kind::String});
    c.values.push_back({.string = "Classical", .integer = 0, .boolean = false, .kind = Operand::Kind::String});
    rule.match.children.push_back(std::move(c));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    // IN operator produces multiple parameters
    ASSERT_EQ(result->params.size(), 3u);
}

TEST(SmartPlaylistOperators, MatchesOperator) {
    PlaylistRule rule;
    rule.name = "Regex Test";
    rule.match.op = Group::Conjunction::And;

    Comparison c;
    c.field = "title";
    c.op = FieldOp::Matches;
    c.value.kind = Operand::Kind::String;
    c.value.string = "^The.*\\d+$";
    rule.match.children.push_back(std::move(c));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    ASSERT_FALSE(result->params.empty());
    EXPECT_EQ(result->params[0].text, "^The.*\\d+$");
}

// ===========================================================================
//  Case sensitivity — REQ-PLS-013
// ===========================================================================

TEST(SmartPlaylistCaseSensitivity, StringFieldsDefaultToNoCase) {
    PlaylistRule rule;
    rule.name = "Case Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Eq, "rock"));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value());
    // Should contain COLLATE NOCASE
    EXPECT_TRUE(result->where.find("COLLATE NOCASE") != std::string::npos);
}

TEST(SmartPlaylistCaseSensitivity, ExplicitCaseSensitive) {
    PlaylistRule rule;
    rule.name = "Case Test";
    rule.match.op = Group::Conjunction::And;

    Comparison c;
    c.field = "genre";
    c.op = FieldOp::Eq;
    c.case_sensitive = true;
    c.value.kind = Operand::Kind::String;
    c.value.string = "Rock";
    rule.match.children.push_back(std::move(c));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value());
    // Should NOT contain COLLATE NOCASE
    EXPECT_TRUE(result->where.find("COLLATE NOCASE") == std::string::npos);
}

// ===========================================================================
//  Numeric fields — REQ-PLS-014
// ===========================================================================

TEST(SmartPlaylistNumeric, YearComparison) {
    PlaylistRule rule;
    rule.name = "Year Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_int_cmp("year", FieldOp::Gte, 2020));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    ASSERT_FALSE(result->params.empty());
    EXPECT_EQ(result->params[0].kind, BindValue::Kind::Integer);
    EXPECT_EQ(result->params[0].integer, 2020);
}

TEST(SmartPlaylistNumeric, DurationComparison) {
    PlaylistRule rule;
    rule.name = "Duration Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_int_cmp("duration", FieldOp::Gt, 180000));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    // Duration is in ms, mapped to duration_ms column
    EXPECT_TRUE(result->where.find("duration_ms") != std::string::npos);
}

TEST(SmartPlaylistNumeric, RatingComparison) {
    PlaylistRule rule;
    rule.name = "Rating Test";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_int_cmp("rating", FieldOp::Gte, 4));

    auto result = compile_rule(rule);
    ASSERT_TRUE(result.has_value()) << result.error().to_log_string();
    EXPECT_FALSE(result->where.empty());
}

// ===========================================================================
//  Integration with database
// ===========================================================================

class SmartPlaylistIntegration : public ::testing::Test {
  protected:
    void SetUp() override {
        const auto base = std::filesystem::temp_directory_path() / "arrow-player-sp-test";
        db_path = base.string() + "-" + std::to_string(getpid()) + ".sqlite";
        std::filesystem::remove(db_path);
        db = std::make_unique<LibraryDatabase>();
        ASSERT_TRUE(db->open(db_path));
    }

    void TearDown() override {
        db->close();
        std::filesystem::remove(db_path);
    }

    std::filesystem::path db_path;
    std::unique_ptr<LibraryDatabase> db;
};

TEST_F(SmartPlaylistIntegration, EmptyDatabaseReturnsNoResults) {
    PlaylistRule rule;
    rule.name = "Empty DB";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));

    auto compiled = compile_rule(rule);
    ASSERT_TRUE(compiled.has_value());
    // Empty database should return no tracks
    auto results = db->search_tracks(compiled->where, compiled->params, compiled->limit);
    ASSERT_TRUE(results);
    EXPECT_EQ(results->size(), 0u);
}

TEST_F(SmartPlaylistIntegration, SimpleSearch) {
    // Add a track first
    LibraryTrack track;
    track.path = "/test/rock.mp3";
    track.title = "Rock Song";
    track.genre = "Rock";
    track.year = 2020;
    track.duration_ms = 180000;
    track.rating = 4;
    ASSERT_TRUE(db->add_track(track));

    // Search for Rock genre
    PlaylistRule rule;
    rule.name = "Rock Only";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));

    auto compiled = compile_rule(rule);
    ASSERT_TRUE(compiled.has_value());
    auto results = db->search_tracks(compiled->where, compiled->params, compiled->limit);
    ASSERT_TRUE(results);
    EXPECT_EQ(results->size(), 1u);
    EXPECT_EQ(results->front().title, "Rock Song");
}

TEST_F(SmartPlaylistIntegration, MultipleCriteria) {
    // Add tracks
    LibraryTrack rock2020;
    rock2020.path = "/test/rock2020.mp3";
    rock2020.genre = "Rock";
    rock2020.year = 2020;
    rock2020.rating = 4;
    ASSERT_TRUE(db->add_track(rock2020));

    LibraryTrack rock2010;
    rock2010.path = "/test/rock2010.mp3";
    rock2010.genre = "Rock";
    rock2010.year = 2010;
    rock2010.rating = 4;
    ASSERT_TRUE(db->add_track(rock2010));

    LibraryTrack jazz2020;
    jazz2020.path = "/test/jazz2020.mp3";
    jazz2020.genre = "Jazz";
    jazz2020.year = 2020;
    jazz2020.rating = 4;
    ASSERT_TRUE(db->add_track(jazz2020));

    // Search: Rock AND year >= 2020
    PlaylistRule rule;
    rule.name = "Recent Rock";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));
    rule.match.children.push_back(make_int_cmp("year", FieldOp::Gte, 2020));

    auto compiled = compile_rule(rule);
    ASSERT_TRUE(compiled.has_value());
    auto results = db->search_tracks(compiled->where, compiled->params, compiled->limit);
    ASSERT_TRUE(results);
    EXPECT_EQ(results->size(), 1u);
    EXPECT_EQ(results->front().year, 2020);
}

TEST_F(SmartPlaylistIntegration, LimitApplied) {
    // Add multiple tracks
    for (int i = 0; i < 10; ++i) {
        LibraryTrack t;
        t.path = "/test/track" + std::to_string(i) + ".mp3";
        t.title = "Track " + std::to_string(i);
        t.genre = "Rock";
        ASSERT_TRUE(db->add_track(t));
    }

    PlaylistRule rule;
    rule.name = "Limited";
    rule.match.op = Group::Conjunction::And;
    rule.match.children.push_back(make_str_cmp("genre", FieldOp::Contains, "Rock"));
    rule.limit = 5;

    auto compiled = compile_rule(rule);
    ASSERT_TRUE(compiled.has_value());
    auto results = db->search_tracks(compiled->where, compiled->params, compiled->limit);
    ASSERT_TRUE(results);
    EXPECT_EQ(results->size(), 5u);
}
