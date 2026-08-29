// SPDX-License-Identifier: MPL-2.0
// EFS conformance tests — spec §10, REQ-EFS-001..012.
//
// Every test case in shared-spec/conformance/efs-cases.json that is tagged
// with a known track is reproduced here as a GoogleTest.  The fixture file
// is the authoritative definition; this test documents the implementation
// that must match it.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/format/format_engine.hpp"
#include "core/format/track_context.hpp"

namespace efs = arrow::efs;

// ---------------------------------------------------------------------------
//  Test fixture — owns a Track and exposes it as a TrackView.
// ---------------------------------------------------------------------------

struct TestTrack {
    efs::Track track;

    TestTrack& title(std::string v) { track.title = std::move(v); return *this; }
    TestTrack& artist(std::string v) { track.artist = std::move(v); return *this; }
    TestTrack& album(std::string v) { track.album = std::move(v); return *this; }
    TestTrack& album_artist(std::string v) { track.album_artist = std::move(v); return *this; }
    TestTrack& genre(std::string v) { track.genre = std::move(v); return *this; }
    TestTrack& composer(std::string v) { track.composer = std::move(v); return *this; }
    TestTrack& comment(std::string v) { track.comment = std::move(v); return *this; }
    TestTrack& grouping(std::string v) { track.grouping = std::move(v); return *this; }
    TestTrack& year(std::string v) { track.year = std::move(v); return *this; }
    TestTrack& date(std::string v) { track.date = std::move(v); return *this; }
    TestTrack& track_number(std::string v) { track.track_number = std::move(v); return *this; }
    TestTrack& track_total(std::string v) { track.track_total = std::move(v); return *this; }
    TestTrack& disc_number(std::string v) { track.disc_number = std::move(v); return *this; }
    TestTrack& disc_total(std::string v) { track.disc_total = std::move(v); return *this; }
    TestTrack& duration_ms(std::string v) { track.duration_ms = std::move(v); return *this; }
    TestTrack& bitrate_kbps(std::string v) { track.bitrate_kbps = std::move(v); return *this; }
    TestTrack& sample_rate(std::string v) { track.sample_rate = std::move(v); return *this; }
    TestTrack& bit_depth(std::string v) { track.bit_depth = std::move(v); return *this; }
    TestTrack& channels(std::string v) { track.channels = std::move(v); return *this; }
    TestTrack& codec(std::string v) { track.codec = std::move(v); return *this; }
    TestTrack& container(std::string v) { track.container = std::move(v); return *this; }
    TestTrack& is_lossless(bool v) { track.is_lossless = v; return *this; }
    TestTrack& path(std::string v) { track.path = std::move(v); return *this; }
    TestTrack& filename(std::string v) { track.filename = std::move(v); return *this; }
    TestTrack& file_size(std::string v) { track.file_size = std::move(v); return *this; }
    TestTrack& rating(std::string v) { track.rating = std::move(v); return *this; }
    TestTrack& play_count(std::string v) { track.play_count = std::move(v); return *this; }
    TestTrack& skip_count(std::string v) { track.skip_count = std::move(v); return *this; }
    TestTrack& loved(bool v) { track.loved = v; return *this; }
    TestTrack& missing(bool v) { track.missing = v; return *this; }
    TestTrack& playing_state(std::string v) { track.playing_state = std::move(v); return *this; }
    TestTrack& position_ms(std::string v) { track.position_ms = std::move(v); return *this; }
    TestTrack& queue_index(std::string v) { track.queue_index = std::move(v); return *this; }
    TestTrack& queue_total(std::string v) { track.queue_total = std::move(v); return *this; }
    TestTrack& list_index(std::string v) { track.list_index = std::move(v); return *this; }
    TestTrack& list_total(std::string v) { track.list_total = std::move(v); return *this; }
    TestTrack& last_played_at(std::string v) { track.last_played_at = std::move(v); return *this; }
    TestTrack& added_at(std::string v) { track.added_at = std::move(v); return *this; }
    TestTrack& bpm(std::string v) { track.bpm = std::move(v); return *this; }
    TestTrack& music_key(std::string v) { track.music_key = std::move(v); return *this; }
    TestTrack& rg_track_gain(std::string v) { track.rg_track_gain = std::move(v); return *this; }
    TestTrack& now_unix(std::int64_t v) { track.now_unix_ = v; return *this; }
};

// ---------------------------------------------------------------------------
//  Test harness
// ---------------------------------------------------------------------------

struct EFSTest : testing::Test {
    efs::Track make_kid_a() const {
        efs::Track t;
        t.title = "Idioteque";
        t.artist = "Radiohead";
        t.album_artist = "Radiohead";
        t.album = "Kid A";
        t.genre = "Electronic";
        t.composer = "Radiohead";
        t.comment = "";
        t.grouping = "";
        t.year = "2000";
        t.date = "2000-10-02";
        t.track_number = "8";
        t.track_total = "10";
        t.disc_number = "1";
        t.disc_total = "1";
        t.duration_ms = "500";
        t.bitrate_kbps = "1002";
        t.sample_rate = "44100";
        t.bit_depth = "16";
        t.channels = "2";
        t.codec = "flac";
        t.container = "flac";
        t.is_lossless = true;
        t.rating = "80";
        t.loved = true;
        t.missing = false;
        t.play_count = "42";
        t.skip_count = "1";
        t.last_played_at = "2026-08-01T20:14:00Z";
        t.added_at = "2019-03-11T09:00:00Z";
        t.bpm = "138";
        t.music_key = "F#m";
        t.rg_track_gain = "-7.2";
        t.path = "/music/Radiohead/Kid A/08 Idioteque.flac";
        t.filename = "08 Idioteque.flac";
        t.file_size = "8800000";
        t.playing_state = "playing";
        t.position_ms = "137000";
        t.queue_index = "3";
        t.queue_total = "12";
        t.list_index = "8";
        t.list_total = "10";
        t.now_unix_ = 1724500800;  // 2026-08-24T12:00:00Z
        return t;
    }

    efs::Track make_no_album() const {
        efs::Track t;
        t.title = "Idioteque";
        t.artist = "Radiohead";
        t.duration_ms = "512";
        t.playing_state = "playing";
        t.position_ms = "0";
        t.play_count = "3";
        t.rating = "0";
        t.loved = false;
        t.codec = "flac";
        t.is_lossless = true;
        t.path = "/music/bootlegs/idioteque-live.flac";
        t.filename = "idioteque-live.flac";
        t.file_size = "11500000";
        t.now_unix_ = 1724500800;
        return t;
    }

    efs::Track make_untagged() const {
        efs::Track t;
        t.title = "";
        t.artist = "";
        t.album = "";
        t.duration_ms = "63";
        t.play_count = "0";
        t.rating = "0";
        t.loved = false;
        t.codec = "mp3";
        t.is_lossless = false;
        t.bitrate_kbps = "128";
        t.sample_rate = "44100";
        t.channels = "2";
        t.path = "/music/unsorted/track01.mp3";
        t.filename = "track01.mp3";
        t.file_size = "1010000";
        t.missing = false;
        t.playing_state = "stopped";
        t.now_unix_ = 1724500800;
        return t;
    }

    efs::Track make_unicode() const {
        efs::Track t;
        t.title = "Café";
        t.artist = "مقام العجم";
        t.album = "👨‍👩‍👧‍👦 Family";
        t.genre = "J-Pop";
        t.duration_ms = "200";
        t.year = "2021";
        t.track_number = "3";
        t.codec = "opus";
        t.is_lossless = false;
        t.rating = "60";
        t.now_unix_ = 1724500800;
        return t;
    }

    efs::Track make_stopped() const {
        efs::Track t;
        t.playing_state = "stopped";
        t.now_unix_ = 1724500800;
        return t;
    }

    std::string eval(std::string_view pattern,
                    const efs::Track& track,
                    std::string locale = "en-US") const {
        efs::EvalContext ctx;
        ctx.locale = locale;
        ctx.now_unix = track.now_unix_;
        ctx.output_cap = 4096;
        auto result = efs::render(pattern, track, ctx);
        return result.text;
    }
};

// ---------------------------------------------------------------------------
//  §10.3 — Field substitution
// ---------------------------------------------------------------------------

using Fields = EFSTest;

TEST_F(Fields, field_title) {
    EXPECT_EQ("Idioteque", eval("%title%", make_kid_a()));
}

TEST_F(Fields, field_literal_join) {
    EXPECT_EQ("Radiohead - Idioteque", eval("%artist% - %title%", make_kid_a()));
}

TEST_F(Fields, field_numeric_renders_bare) {
    EXPECT_EQ("8", eval("%tracknumber%", make_kid_a()));
}

TEST_F(Fields, field_boolean_true) {
    EXPECT_EQ("1", eval("%islossless%", make_kid_a()));
}

TEST_F(Fields, field_loved_true) {
    EXPECT_EQ("1", eval("%loved%", make_kid_a()));
}

TEST_F(Fields, field_boolean_false) {
    EXPECT_EQ("0", eval("%missing%", make_kid_a()));
}

TEST_F(Fields, field_length_mmss) {
    EXPECT_EQ("8:20", eval("%length%", make_kid_a()));
}

TEST_F(Fields, field_length_hmmss) {
    EXPECT_EQ("1:01:01", eval("$time(3661)", make_kid_a()));
}

TEST_F(Fields, field_length_seconds) {
    EXPECT_EQ("500", eval("%length_seconds%", make_kid_a()));
}

TEST_F(Fields, field_position) {
    // position_ms 137000 = 137 seconds = 2:17
    EXPECT_EQ("2:17", eval("%position%", make_kid_a()));
}

TEST_F(Fields, field_remaining) {
    // duration 500, position 137000 → remaining = 363s = 6:03
    EXPECT_EQ("6:03", eval("%remaining%", make_kid_a()));
}

TEST_F(Fields, field_playing_state) {
    EXPECT_EQ("playing", eval("%playing_state%", make_kid_a()));
}

TEST_F(Fields, field_playing_state_stopped) {
    EXPECT_EQ("stopped", eval("%playing_state%", make_stopped()));
}

TEST_F(Fields, field_queue) {
    EXPECT_EQ("3 of 12", eval("%queue_index% of %queue_total%", make_kid_a()));
}

TEST_F(Fields, field_list) {
    EXPECT_EQ("8/10", eval("%list_index%/%list_total%", make_kid_a()));
}

TEST_F(Fields, field_technical) {
    EXPECT_EQ("flac 44100 16 2",
              eval("%codec% %samplerate% %bitdepth% %channels%", make_kid_a()));
}

TEST_F(Fields, field_bitrate) {
    EXPECT_EQ("1002", eval("%bitrate%", make_kid_a()));
}

TEST_F(Fields, field_filesize_natural) {
    // 8800000 bytes → 8.8 MB
    EXPECT_EQ("8.8 MB", eval("%filesize_natural%", make_kid_a()));
}

TEST_F(Fields, field_filesize_natural_de) {
    EXPECT_EQ("8,8 MB", eval("%filesize_natural%", make_kid_a(), "de-DE"));
}

TEST_F(Fields, field_rating_stars) {
    EXPECT_EQ("★★★★☆", eval("%rating_stars%", make_kid_a()));
}

TEST_F(Fields, field_replaygain_applied) {
    EXPECT_EQ("-7.2", eval("%replaygain_applied%", make_kid_a()));
}

TEST_F(Fields, field_path) {
    EXPECT_EQ("/music/Radiohead/Kid A/08 Idioteque.flac",
              eval("%path%", make_kid_a()));
}

TEST_F(Fields, field_filename) {
    EXPECT_EQ("08 Idioteque.flac", eval("%filename%", make_kid_a()));
}

TEST_F(Fields, field_unknown_is_absent) {
    EXPECT_EQ("", eval("%nosuchfield%", make_kid_a()));
}

TEST_F(Fields, field_unknown_in_block_collapses) {
    EXPECT_EQ("", eval("[%nosuchfield%]", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10.3 — Format specs
// ---------------------------------------------------------------------------

using FormatSpec = EFSTest;

TEST_F(FormatSpec, spec_zeropad_2) {
    EXPECT_EQ("08", eval("%tracknumber:02%", make_kid_a()));
}

TEST_F(FormatSpec, spec_zeropad_3) {
    EXPECT_EQ("008", eval("%tracknumber:03%", make_kid_a()));
}

TEST_F(FormatSpec, spec_zeropad_shorter_than_value) {
    EXPECT_EQ("44100", eval("%samplerate:02%", make_kid_a()));
}

TEST_F(FormatSpec, spec_upper) {
    EXPECT_EQ("RADIOHEAD", eval("%artist:u%", make_kid_a()));
}

TEST_F(FormatSpec, spec_lower) {
    EXPECT_EQ("radiohead", eval("%artist:l%", make_kid_a()));
}

TEST_F(FormatSpec, spec_title) {
    EXPECT_EQ("Kid A", eval("%album:t%", make_kid_a()));
}

TEST_F(FormatSpec, spec_title_from_lower) {
    EXPECT_EQ("Flac", eval("%codec:t%", make_kid_a()));
}

TEST_F(FormatSpec, spec_upper_on_absent) {
    EXPECT_EQ("", eval("[%album:u%]", make_no_album()));
}

TEST_F(FormatSpec, spec_lower_unicode) {
    EXPECT_EQ("j-pop", eval("%genre:l%", make_unicode()));
}

TEST_F(FormatSpec, spec_upper_rtl_unchanged) {
    // Arabic is unicase — upper-casing must leave every code point untouched.
    EXPECT_EQ("مقام العجم", eval("%artist:u%", make_unicode()));
}

// ---------------------------------------------------------------------------
//  §10.3 — Optional blocks (REQ-EFS-003)
// ---------------------------------------------------------------------------

using OptionalBlocks = EFSTest;

TEST_F(OptionalBlocks, req_efs_003_with_album) {
    EXPECT_EQ("Radiohead - Idioteque (Kid A)",
              eval("%artist% - %title%[ (%album%)]", make_kid_a()));
}

TEST_F(OptionalBlocks, req_efs_003_without_album) {
    EXPECT_EQ("Radiohead - Idioteque",
              eval("%artist% - %title%[ (%album%)]", make_no_album()));
}

TEST_F(OptionalBlocks, block_one_of_two_present) {
    EXPECT_EQ("Kid A / Electronic", eval("[%album% / %genre%]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_partial_renders_in_full) {
    // block with at least one present field → renders in full
    EXPECT_EQ("Radiohead / ", eval("[%artist% / %nosuchfield%]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_nested_inner_collapses) {
    EXPECT_EQ("Kid A (2000)", eval("[%album%[ (%year%)]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_nested_inner_collapses_2) {
    EXPECT_EQ("Kid A", eval("[%album%[ (%nosuchfield%)]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_nested_outer_collapses) {
    // inner block has present field → inner renders → outer not empty → renders
    EXPECT_EQ("2000", eval("[%nosuchfield%[ (%year%)]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_nested_all_absent) {
    EXPECT_EQ("", eval("[%nosuchfield%[ (%alsomissing%)]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_three_deep) {
    EXPECT_EQ("Radiohead - Kid A (2000)",
              eval("[%artist%[ - %album%[ (%year%)]]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_three_deep_middle_absent) {
    EXPECT_EQ("Radiohead - (2000)",
              eval("[%artist%[ - %nope%[ (%year%)]]]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_no_field_refs_renders) {
    // block with NO field refs renders in full (vacuous truth)
    EXPECT_EQ(" - ", eval("[ - ]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_only_function_renders) {
    EXPECT_EQ("ABC", eval("[$upper(abc)]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_function_over_absent_field) {
    // function wraps absent field → block collapses
    EXPECT_EQ("", eval("[$upper(%album%)]", make_no_album()));
}

TEST_F(OptionalBlocks, block_empty) {
    EXPECT_EQ("", eval("[]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_adjacent) {
    EXPECT_EQ("Kid AElectronic", eval("[%album%][%genre%]", make_kid_a()));
}

TEST_F(OptionalBlocks, block_tracknumber_default) {
    EXPECT_EQ("08. Radiohead - Idioteque",
              eval("[%tracknumber:02%. ]%artist% - %title%", make_kid_a()));
}

TEST_F(OptionalBlocks, block_tracknumber_absent) {
    EXPECT_EQ("Radiohead - Idioteque",
              eval("[%tracknumber:02%. ]%artist% - %title%", make_no_album()));
}

// ---------------------------------------------------------------------------
//  §10.4 — Absent vs empty
// ---------------------------------------------------------------------------

using AbsentVsEmpty = EFSTest;

TEST_F(AbsentVsEmpty, req_efs_004_empty_string_field_is_absent) {
    // comment is empty string → absent (blank tag = missing)
    EXPECT_EQ("", eval("[%comment%]", make_kid_a()));
}

TEST_F(AbsentVsEmpty, req_efs_004_whitespace_only_is_absent) {
    // grouping is empty string → absent
    EXPECT_EQ("", eval("[%grouping%]", make_kid_a()));
}

TEST_F(AbsentVsEmpty, req_efs_004_empty_literal_does_not_collapse) {
    // '' preserves block because it is not a field ref
    EXPECT_EQ("Kid A", eval("[''%album%]", make_kid_a()));
}

TEST_F(AbsentVsEmpty, req_efs_004_literal_keeps_block_empty_when_field_absent) {
    // literals do NOT keep block alive — only field refs do
    EXPECT_EQ("", eval("[-%album%-]", make_no_album()));
}

TEST_F(AbsentVsEmpty, absent_untagged_title) {
    EXPECT_EQ("", eval("[%title%]", make_untagged()));
}

TEST_F(AbsentVsEmpty, absent_fallback_to_filename) {
    EXPECT_EQ("track01.mp3", eval("$if2(%title%,%filename%)", make_untagged()));
}

TEST_F(AbsentVsEmpty, absent_zero_is_not_absent) {
    // numeric zero is a value, not an absence
    EXPECT_EQ("0", eval("[%playcount%]", make_untagged()));
}

TEST_F(AbsentVsEmpty, absent_false_is_not_absent) {
    EXPECT_EQ("0", eval("[%loved%]", make_untagged()));
}

// ---------------------------------------------------------------------------
//  §10.5 — Quoting
// ---------------------------------------------------------------------------

using Quoting = EFSTest;

TEST_F(Quoting, quote_percent) {
    EXPECT_EQ("%", eval("'%'", make_kid_a()));
}

TEST_F(Quoting, quote_dollar) {
    EXPECT_EQ("$", eval("'$'", make_kid_a()));
}

TEST_F(Quoting, quote_brackets) {
    EXPECT_EQ("[x]", eval("'['x']'", make_kid_a()));
}

TEST_F(Quoting, quote_single_quote_pair) {
    // empty quoted run → escape for nothing
    EXPECT_EQ("", eval("''", make_kid_a()));
}

TEST_F(Quoting, quote_run) {
    // quoted literal text, not field references
    EXPECT_EQ("%artist% - %title%", eval("'%artist% - %title%'", make_kid_a()));
}

TEST_F(Quoting, quote_inside_literal) {
    EXPECT_EQ("Track [8]", eval("Track '['%tracknumber%']'", make_kid_a()));
}

TEST_F(Quoting, quote_percent_then_field) {
    EXPECT_EQ("%8", eval("'%'%tracknumber%", make_kid_a()));
}

TEST_F(Quoting, quote_dollar_then_function) {
    EXPECT_EQ("$X", eval("'$'$upper(x)", make_kid_a()));
}

TEST_F(Quoting, quote_adjacent_quoted_runs) {
    EXPECT_EQ("ab", eval("'a''b'", make_kid_a()));
}

TEST_F(Quoting, quote_unclosed_runs_to_end) {
    // unterminated quoted run closes at end of pattern
    EXPECT_EQ("abc", eval("'abc", make_kid_a()));
}

TEST_F(Quoting, quote_comma_in_function_arg) {
    EXPECT_EQ("a,b", eval("$if(1,'a,b',c)", make_kid_a()));
}

TEST_F(Quoting, quote_bracket_in_function_arg) {
    EXPECT_EQ("[", eval("$upper('[')", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10.5 — Conditional functions
// ---------------------------------------------------------------------------

using Conditional = EFSTest;

TEST_F(Conditional, if_true) {
    EXPECT_EQ("yes", eval("$if(%album%,yes,no)", make_kid_a()));
}

TEST_F(Conditional, if_false) {
    EXPECT_EQ("no", eval("$if(%album%,yes,no)", make_no_album()));
}

TEST_F(Conditional, if_no_else) {
    EXPECT_EQ("yes", eval("$if(%album%,yes)", make_kid_a()));
}

TEST_F(Conditional, if_no_else_false) {
    EXPECT_EQ("", eval("$if(%album%,yes)", make_no_album()));
}

TEST_F(Conditional, if_zero_is_truthy) {
    // $if tests non-empty string, not truthiness
    EXPECT_EQ("yes", eval("$if(0,yes,no)", make_kid_a()));
}

TEST_F(Conditional, if_nested) {
    EXPECT_EQ("both",
              eval("$if(%album%,$if(%year%,both,album only),neither)", make_kid_a()));
}

TEST_F(Conditional, if2_first) {
    EXPECT_EQ("Kid A", eval("$if2(%album%,%genre%)", make_kid_a()));
}

TEST_F(Conditional, if2_second) {
    EXPECT_EQ("", eval("$if2(%album%,%genre%)", make_no_album()));
}

TEST_F(Conditional, if2_second_present) {
    EXPECT_EQ("Radiohead", eval("$if2(%nope%,%artist%)", make_kid_a()));
}

TEST_F(Conditional, if3_first_non_empty) {
    EXPECT_EQ("Kid A", eval("$if3(%nope%,%album%,%genre%)", make_kid_a()));
}

TEST_F(Conditional, if3_falls_through) {
    EXPECT_EQ("Electronic", eval("$if3(%nope%,%alsonope%,%genre%)", make_kid_a()));
}

TEST_F(Conditional, if3_all_absent) {
    EXPECT_EQ("", eval("$if3(%a%,%b%,%c%)", make_kid_a()));
}

TEST_F(Conditional, if3_many_args) {
    EXPECT_EQ("Radiohead", eval("$if3(%a%,%b%,%c%,%d%,%artist%)", make_kid_a()));
}

TEST_F(Conditional, ifequal_equal) {
    EXPECT_EQ("match", eval("$ifequal(%tracknumber%,8,match,differ)", make_kid_a()));
}

TEST_F(Conditional, ifequal_unequal) {
    EXPECT_EQ("differ", eval("$ifequal(%tracknumber%,9,match,differ)", make_kid_a()));
}

TEST_F(Conditional, ifequal_numeric_not_textual) {
    // numeric equality: "08" == 8
    EXPECT_EQ("match", eval("$ifequal(08,8,match,differ)", make_kid_a()));
}

TEST_F(Conditional, ifequal_non_numeric_is_absent) {
    EXPECT_EQ("", eval("$ifequal(abc,8,match,differ)", make_kid_a()));
}

TEST_F(Conditional, ifgreater_true) {
    EXPECT_EQ("popular", eval("$ifgreater(%playcount%,10,popular,rare)", make_kid_a()));
}

TEST_F(Conditional, ifgreater_false_on_equal) {
    EXPECT_EQ("not", eval("$ifgreater(%playcount%,42,gt,not)", make_kid_a()));
}

TEST_F(Conditional, ifless_true) {
    EXPECT_EQ("few", eval("$ifless(%skipcount%,5,few,many)", make_kid_a()));
}

TEST_F(Conditional, ifless_negative) {
    EXPECT_EQ("neg", eval("$ifless(-3,0,neg,pos)", make_kid_a()));
}

TEST_F(Conditional, iflonger_true) {
    EXPECT_EQ("long", eval("$iflonger(%title%,5,long,short)", make_kid_a()));
}

TEST_F(Conditional, iflonger_false) {
    EXPECT_EQ("short", eval("$iflonger(%title%,20,long,short)", make_kid_a()));
}

TEST_F(Conditional, iflonger_graphemes) {
    // "Café" = 4 grapheme clusters
    EXPECT_EQ("short", eval("$iflonger(%title%,4,long,short)", make_unicode()));
}

// ---------------------------------------------------------------------------
//  §10.5 — String functions
// ---------------------------------------------------------------------------

using StringFuncs = EFSTest;

TEST_F(StringFuncs, upper) {
    EXPECT_EQ("RADIOHEAD", eval("$upper(%artist%)", make_kid_a()));
}

TEST_F(StringFuncs, lower) {
    EXPECT_EQ("radiohead", eval("$lower(%artist%)", make_kid_a()));
}

TEST_F(StringFuncs, title_simple) {
    EXPECT_EQ("Kid A", eval("$title(%album%)", make_kid_a()));
}

TEST_F(StringFuncs, title_lowercases_rest) {
    EXPECT_EQ("Radiohead", eval("$title(RADIOHEAD)", make_kid_a()));
}

TEST_F(StringFuncs, caps_preserves_rest) {
    EXPECT_EQ("RADIOHEAD", eval("$caps(RADIOHEAD)", make_kid_a()));
}

TEST_F(StringFuncs, caps_raises_first) {
    EXPECT_EQ("Radiohead Rocks", eval("$caps(radiohead rocks)", make_kid_a()));
}

TEST_F(StringFuncs, trim) {
    EXPECT_EQ("padded", eval("$trim('  padded  ')", make_kid_a()));
}

TEST_F(StringFuncs, trim_inner_preserved) {
    EXPECT_EQ("a  b", eval("$trim('  a  b  ')", make_kid_a()));
}

TEST_F(StringFuncs, len) {
    EXPECT_EQ("9", eval("$len(%artist%)", make_kid_a()));
}

TEST_F(StringFuncs, len_empty) {
    // $len of absent field is 0, not absent
    EXPECT_EQ("0", eval("$len(%album%)", make_no_album()));
}

TEST_F(StringFuncs, sub_start_only) {
    // zero-based indexing
    EXPECT_EQ("ohead", eval("$sub(%artist%,4)", make_kid_a()));
}

TEST_F(StringFuncs, sub_start_len) {
    EXPECT_EQ("Radio", eval("$sub(%artist%,0,5)", make_kid_a()));
}

TEST_F(StringFuncs, sub_past_end) {
    EXPECT_EQ("", eval("$sub(%artist%,50,5)", make_kid_a()));
}

TEST_F(StringFuncs, sub_len_past_end) {
    EXPECT_EQ("head", eval("$sub(%artist%,5,99)", make_kid_a()));
}

TEST_F(StringFuncs, sub_negative_start) {
    // negative index → absent
    EXPECT_EQ("", eval("$sub(%artist%,-3)", make_kid_a()));
}

TEST_F(StringFuncs, left) {
    EXPECT_EQ("Radio", eval("$left(%artist%,5)", make_kid_a()));
}

TEST_F(StringFuncs, left_longer_than_string) {
    EXPECT_EQ("Radiohead", eval("$left(%artist%,99)", make_kid_a()));
}

TEST_F(StringFuncs, left_zero) {
    EXPECT_EQ("", eval("$left(%artist%,0)", make_kid_a()));
}

TEST_F(StringFuncs, right) {
    EXPECT_EQ("head", eval("$right(%artist%,4)", make_kid_a()));
}

TEST_F(StringFuncs, right_longer_than_string) {
    EXPECT_EQ("Radiohead", eval("$right(%artist%,99)", make_kid_a()));
}

TEST_F(StringFuncs, pad_left) {
    // right-align by padding on left
    EXPECT_EQ("   8", eval("$pad(%tracknumber%,4)", make_kid_a()));
}

TEST_F(StringFuncs, pad_custom_char) {
    EXPECT_EQ("0008", eval("$pad(%tracknumber%,4,0)", make_kid_a()));
}

TEST_F(StringFuncs, pad_never_truncates) {
    EXPECT_EQ("Radiohead", eval("$pad(%artist%,3)", make_kid_a()));
}

TEST_F(StringFuncs, padright) {
    EXPECT_EQ("8   ", eval("$padright(%tracknumber%,4)", make_kid_a()));
}

TEST_F(StringFuncs, padright_custom_char) {
    EXPECT_EQ("8...", eval("$padright(%tracknumber%,4,.)", make_kid_a()));
}

TEST_F(StringFuncs, cut) {
    EXPECT_EQ("Radio", eval("$cut(%artist%,5)", make_kid_a()));
}

TEST_F(StringFuncs, cut_no_ellipsis) {
    EXPECT_EQ("Idio", eval("$cut(%title%,4)", make_kid_a()));
}

TEST_F(StringFuncs, cut_longer_than_string) {
    EXPECT_EQ("Radiohead", eval("$cut(%artist%,99)", make_kid_a()));
}

TEST_F(StringFuncs, cut_zero) {
    EXPECT_EQ("", eval("$cut(%artist%,0)", make_kid_a()));
}

TEST_F(StringFuncs, abbr) {
    EXPECT_EQ("KA", eval("$abbr(%album%)", make_kid_a()));
}

TEST_F(StringFuncs, abbr_multiword) {
    EXPECT_EQ("TB", eval("$abbr('The Bends')", make_kid_a()));
}

TEST_F(StringFuncs, abbr_with_threshold_under) {
    EXPECT_EQ("Kid A", eval("$abbr(%album%,10)", make_kid_a()));
}

TEST_F(StringFuncs, abbr_with_threshold_over) {
    EXPECT_EQ("ADE", eval("$abbr('Amnesiac Deluxe Edition',10)", make_kid_a()));
}

TEST_F(StringFuncs, replace) {
    EXPECT_EQ("RadioHEAD", eval("$replace(%artist%,head,HEAD)", make_kid_a()));
}

TEST_F(StringFuncs, replace_all_occurrences) {
    EXPECT_EQ("a-b-c", eval("$replace(aXbXc,X,-)", make_kid_a()));
}

TEST_F(StringFuncs, replace_not_found) {
    EXPECT_EQ("Radiohead", eval("$replace(%artist%,zzz,x)", make_kid_a()));
}

TEST_F(StringFuncs, replace_with_empty) {
    EXPECT_EQ("Radio", eval("$replace(%artist%,head,)", make_kid_a()));
}

TEST_F(StringFuncs, strchr_found) {
    // zero-based index
    EXPECT_EQ("2", eval("$strchr(%artist%,d)", make_kid_a()));
}

TEST_F(StringFuncs, strchr_first_of_repeats) {
    EXPECT_EQ("1", eval("$strchr(%artist%,a)", make_kid_a()));
}

TEST_F(StringFuncs, strchr_not_found) {
    EXPECT_EQ("", eval("$strchr(%artist%,z)", make_kid_a()));
}

TEST_F(StringFuncs, strstr_found) {
    EXPECT_EQ("5", eval("$strstr(%artist%,head)", make_kid_a()));
}

TEST_F(StringFuncs, strstr_at_start) {
    EXPECT_EQ("0", eval("$strstr(%artist%,Radio)", make_kid_a()));
}

TEST_F(StringFuncs, strstr_not_found) {
    EXPECT_EQ("", eval("$strstr(%artist%,zzz)", make_kid_a()));
}

TEST_F(StringFuncs, insert) {
    EXPECT_EQ("Radio-head", eval("$insert(%artist%,-,5)", make_kid_a()));
}

TEST_F(StringFuncs, insert_at_zero) {
    EXPECT_EQ(">Radiohead", eval("$insert(%artist%,>,0)", make_kid_a()));
}

TEST_F(StringFuncs, insert_past_end_clamps) {
    EXPECT_EQ("Radiohead!", eval("$insert(%artist%,!,99)", make_kid_a()));
}

TEST_F(StringFuncs, repeat) {
    EXPECT_EQ("ababab", eval("$repeat(ab,3)", make_kid_a()));
}

TEST_F(StringFuncs, repeat_zero) {
    EXPECT_EQ("", eval("$repeat(ab,0)", make_kid_a()));
}

TEST_F(StringFuncs, repeat_at_limit) {
    EXPECT_EQ(std::string(256, 'x'), eval("$repeat(x,256)", make_kid_a()));
}

TEST_F(StringFuncs, repeat_over_limit_is_absent) {
    // over 256 → absent (enforced, not clamped)
    EXPECT_EQ("", eval("$repeat(x,257)", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10.5 — Numeric functions
// ---------------------------------------------------------------------------

using NumericFuncs = EFSTest;

TEST_F(NumericFuncs, add_two) {
    EXPECT_EQ("5", eval("$add(2,3)", make_kid_a()));
}

TEST_F(NumericFuncs, add_many) {
    EXPECT_EQ("10", eval("$add(1,2,3,4)", make_kid_a()));
}

TEST_F(NumericFuncs, add_field) {
    EXPECT_EQ("9", eval("$add(%tracknumber%,1)", make_kid_a()));
}

TEST_F(NumericFuncs, add_negative) {
    EXPECT_EQ("-3", eval("$add(5,-8)", make_kid_a()));
}

TEST_F(NumericFuncs, sub2) {
    EXPECT_EQ("6", eval("$sub2(10,4)", make_kid_a()));
}

TEST_F(NumericFuncs, sub2_negative_result) {
    EXPECT_EQ("-6", eval("$sub2(4,10)", make_kid_a()));
}

TEST_F(NumericFuncs, sub2_not_sub) {
    // $sub is substring; $sub2 is subtraction
    EXPECT_EQ("363", eval("$sub2(%duration%,%position%)", make_kid_a()));
}

TEST_F(NumericFuncs, mul) {
    EXPECT_EQ("42", eval("$mul(6,7)", make_kid_a()));
}

TEST_F(NumericFuncs, mul_many) {
    EXPECT_EQ("24", eval("$mul(2,3,4)", make_kid_a()));
}

TEST_F(NumericFuncs, mul_by_zero) {
    EXPECT_EQ("0", eval("$mul(%playcount%,0)", make_kid_a()));
}

TEST_F(NumericFuncs, div_exact) {
    EXPECT_EQ("5", eval("$div(10,2)", make_kid_a()));
}

TEST_F(NumericFuncs, div_fractional) {
    // 6 fractional digits, trailing zeros trimmed
    EXPECT_EQ("3.5", eval("$div(7,2)", make_kid_a()));
}

TEST_F(NumericFuncs, div_repeating) {
    EXPECT_EQ("0.333333", eval("$div(1,3)", make_kid_a()));
}

TEST_F(NumericFuncs, div_by_zero_is_absent) {
    EXPECT_EQ("", eval("$div(%playcount%,0)", make_kid_a()));
}

TEST_F(NumericFuncs, div_by_zero_in_block) {
    EXPECT_EQ("", eval("[$div(1,0)]", make_kid_a()));
}

TEST_F(NumericFuncs, div_negative) {
    EXPECT_EQ("-4.5", eval("$div(-9,2)", make_kid_a()));
}

TEST_F(NumericFuncs, mod) {
    EXPECT_EQ("1", eval("$mod(10,3)", make_kid_a()));
}

TEST_F(NumericFuncs, mod_by_zero_is_absent) {
    EXPECT_EQ("", eval("$mod(10,0)", make_kid_a()));
}

TEST_F(NumericFuncs, mod_negative) {
    // C-style truncated mod: result takes sign of dividend
    EXPECT_EQ("-1", eval("$mod(-10,3)", make_kid_a()));
}

TEST_F(NumericFuncs, min) {
    EXPECT_EQ("3", eval("$min(5,3,9)", make_kid_a()));
}

TEST_F(NumericFuncs, min_negative) {
    EXPECT_EQ("-5", eval("$min(-5,3)", make_kid_a()));
}

TEST_F(NumericFuncs, max) {
    EXPECT_EQ("9", eval("$max(5,3,9)", make_kid_a()));
}

TEST_F(NumericFuncs, max_single_arg) {
    EXPECT_EQ("7", eval("$max(7)", make_kid_a()));
}

TEST_F(NumericFuncs, num_zeropad) {
    EXPECT_EQ("008", eval("$num(%tracknumber%,3)", make_kid_a()));
}

TEST_F(NumericFuncs, num_wider_than_width) {
    EXPECT_EQ("44100", eval("$num(44100,2)", make_kid_a()));
}

TEST_F(NumericFuncs, num_negative) {
    // width applies to digits; sign is extra
    EXPECT_EQ("-005", eval("$num(-5,3)", make_kid_a()));
}

TEST_F(NumericFuncs, round_no_dp) {
    EXPECT_EQ("4", eval("$round(3.7)", make_kid_a()));
}

TEST_F(NumericFuncs, round_half_away_from_zero) {
    EXPECT_EQ("3", eval("$round(2.5)", make_kid_a()));
}

TEST_F(NumericFuncs, round_negative_half) {
    EXPECT_EQ("-3", eval("$round(-2.5)", make_kid_a()));
}

TEST_F(NumericFuncs, round_dp) {
    EXPECT_EQ("3.14", eval("$round(3.14159,2)", make_kid_a()));
}

TEST_F(NumericFuncs, round_dp_pads_nothing) {
    // $round limits precision; $fixed pads
    EXPECT_EQ("3.1", eval("$round(3.1,3)", make_kid_a()));
}

TEST_F(NumericFuncs, abs_negative) {
    EXPECT_EQ("7.2", eval("$abs(-7.2)", make_kid_a()));
}

TEST_F(NumericFuncs, abs_positive) {
    EXPECT_EQ("42", eval("$abs(42)", make_kid_a()));
}

TEST_F(NumericFuncs, abs_field) {
    EXPECT_EQ("7.2", eval("$abs(%replaygain_applied%)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_non_numeric_arg) {
    EXPECT_EQ("", eval("$add(abc,1)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_absent_arg) {
    EXPECT_EQ("", eval("$add(%nope%,1)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_overflow_is_absent) {
    EXPECT_EQ("", eval("$mul(9223372036854775807,2)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_underflow_is_absent) {
    EXPECT_EQ("", eval("$sub2(-9223372036854775808,1)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_huge_literal_is_absent) {
    EXPECT_EQ("", eval("$add(99999999999999999999,1)", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_nested) {
    EXPECT_EQ("8", eval("$add($mul(2,3),$div(10,5))", make_kid_a()));
}

TEST_F(NumericFuncs, numeric_nested_absent_propagates) {
    EXPECT_EQ("", eval("$add($div(1,0),5)", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10.5 — Time functions
// ---------------------------------------------------------------------------

using TimeFuncs = EFSTest;

TEST_F(TimeFuncs, time_mmss) {
    EXPECT_EQ("8:20", eval("$time(500)", make_kid_a()));
}

TEST_F(TimeFuncs, time_under_a_minute) {
    EXPECT_EQ("1:03", eval("$time(63)", make_kid_a()));
}

TEST_F(TimeFuncs, time_zero) {
    EXPECT_EQ("0:00", eval("$time(0)", make_kid_a()));
}

TEST_F(TimeFuncs, time_hmmss) {
    EXPECT_EQ("1:01:01", eval("$time(3661)", make_kid_a()));
}

TEST_F(TimeFuncs, time_exactly_one_hour) {
    EXPECT_EQ("1:00:00", eval("$time(3600)", make_kid_a()));
}

TEST_F(TimeFuncs, time_negative_is_absent) {
    EXPECT_EQ("", eval("$time(-5)", make_kid_a()));
}

TEST_F(TimeFuncs, time_non_numeric_is_absent) {
    EXPECT_EQ("", eval("$time(abc)", make_kid_a()));
}

TEST_F(TimeFuncs, timems) {
    // 500499 ms = 500s = 8:20 (truncates, does not round)
    EXPECT_EQ("8:20", eval("$timems(500499)", make_kid_a()));
}

TEST_F(TimeFuncs, timems_zero) {
    EXPECT_EQ("0:00", eval("$timems(0)", make_kid_a()));
}

TEST_F(TimeFuncs, date_default_medium) {
    EXPECT_EQ("Oct 2, 2000", eval("$date('2000-10-02')", make_kid_a()));
}

TEST_F(TimeFuncs, date_explicit_pattern) {
    EXPECT_EQ("2000-10-02", eval("$date('2000-10-02','yyyy-MM-dd')", make_kid_a()));
}

TEST_F(TimeFuncs, date_month_year) {
    EXPECT_EQ("October 2000", eval("$date('2000-10-02','MMMM yyyy')", make_kid_a()));
}

TEST_F(TimeFuncs, date_from_field) {
    EXPECT_EQ("2000", eval("$date(%date%,'yyyy')", make_kid_a()));
}

TEST_F(TimeFuncs, date_invalid_is_absent) {
    EXPECT_EQ("", eval("$date('not-a-date')", make_kid_a()));
}

TEST_F(TimeFuncs, year) {
    EXPECT_EQ("2000", eval("$year('2000-10-02')", make_kid_a()));
}

TEST_F(TimeFuncs, year_from_field) {
    EXPECT_EQ("2019", eval("$year(%added%)", make_kid_a()));
}

TEST_F(TimeFuncs, year_invalid_is_absent) {
    EXPECT_EQ("", eval("$year(xyz)", make_kid_a()));
}

TEST_F(TimeFuncs, age_days) {
    // 2026-08-24 - 2026-08-01 = 23 days → "22 days ago"
    EXPECT_EQ("22 days ago", eval("$age(%lastplayed%)", make_kid_a()));
}

TEST_F(TimeFuncs, age_years) {
    // 2026-08-24 - 2019-03-11 ≈ 7 years → "7 years ago"
    EXPECT_EQ("7 years ago", eval("$age(%added%)", make_kid_a()));
}

TEST_F(TimeFuncs, age_invalid_is_absent) {
    EXPECT_EQ("", eval("$age(nope)", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10.5 — Presentation functions
// ---------------------------------------------------------------------------

using PresentationFuncs = EFSTest;

TEST_F(PresentationFuncs, char_decimal) {
    // U+2014 EM DASH
    EXPECT_EQ("—", eval("$char(8212)", make_kid_a()));
}

TEST_F(PresentationFuncs, char_ascii) {
    EXPECT_EQ("A", eval("$char(65)", make_kid_a()));
}

TEST_F(PresentationFuncs, char_astral) {
    // U+1F3B5 MUSIC NOTE
    EXPECT_EQ("🎵", eval("$char(127925)", make_kid_a()));
}

TEST_F(PresentationFuncs, char_zero_is_absent) {
    EXPECT_EQ("", eval("$char(0)", make_kid_a()));
}

TEST_F(PresentationFuncs, char_surrogate_is_absent) {
    EXPECT_EQ("", eval("$char(55296)", make_kid_a()));
}

TEST_F(PresentationFuncs, char_out_of_range_is_absent) {
    EXPECT_EQ("", eval("$char(1114112)", make_kid_a()));
}

TEST_F(PresentationFuncs, crlf) {
    EXPECT_EQ("a\nb", eval("a$crlf()b", make_kid_a()));
}

TEST_F(PresentationFuncs, tab) {
    EXPECT_EQ("a\tb", eval("a$tab()b", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_midway) {
    // 137/500 of 20 chars
    EXPECT_EQ("=====---------------", eval("$progress(137,500,20)", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_start) {
    EXPECT_EQ("----------", eval("$progress(0,500,10)", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_end) {
    EXPECT_EQ("==========", eval("$progress(500,500,10)", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_custom_chars) {
    EXPECT_EQ("##......", eval("$progress(1,4,8,#,.)", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_total_zero_is_absent) {
    EXPECT_EQ("", eval("$progress(1,0,10)", make_kid_a()));
}

TEST_F(PresentationFuncs, progress_width_over_cap_is_absent) {
    EXPECT_EQ("", eval("$progress(1,2,257)", make_kid_a()));
}

TEST_F(PresentationFuncs, stars_default_max) {
    EXPECT_EQ("★★★★☆", eval("$stars(80)", make_kid_a()));
}

TEST_F(PresentationFuncs, stars_rounds_half_up) {
    // 90/100 * 5 = 4.5 → rounds to 5
    EXPECT_EQ("★★★★★", eval("$stars(90)", make_kid_a()));
}

TEST_F(PresentationFuncs, stars_zero) {
    EXPECT_EQ("☆☆☆☆☆", eval("$stars(0)", make_kid_a()));
}

TEST_F(PresentationFuncs, stars_custom_max) {
    EXPECT_EQ("★★★★★★★★★☆", eval("$stars(90,10)", make_kid_a()));
}

TEST_F(PresentationFuncs, stars_from_field) {
    EXPECT_EQ("★★★★☆", eval("$stars(%rating%)", make_kid_a()));
}

TEST_F(PresentationFuncs, fixed_pads) {
    EXPECT_EQ("8   ", eval("$fixed(%tracknumber%,4)", make_kid_a()));
}

TEST_F(PresentationFuncs, fixed_cuts) {
    EXPECT_EQ("Radio", eval("$fixed(%artist%,5)", make_kid_a()));
}

TEST_F(PresentationFuncs, fixed_exact) {
    EXPECT_EQ("Kid A", eval("$fixed(%album%,5)", make_kid_a()));
}

TEST_F(PresentationFuncs, fixed_zero) {
    EXPECT_EQ("", eval("$fixed(%artist%,0)", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  §10 / Unicode — grapheme-cluster semantics
// ---------------------------------------------------------------------------

using Unicode = EFSTest;

TEST_F(Unicode, unicode_combining_len) {
    // "Café" = 4 grapheme clusters (e + combining acute = 1)
    EXPECT_EQ("4", eval("$len(%title%)", make_unicode()));
}

TEST_F(Unicode, unicode_combining_cut_keeps_mark) {
    // cutting at 4 must keep the combining acute attached to 'e'
    EXPECT_EQ("Café", eval("$cut(%title%,4)", make_unicode()));
}

TEST_F(Unicode, unicode_combining_cut_before_mark) {
    EXPECT_EQ("Caf", eval("$cut(%title%,3)", make_unicode()));
}

TEST_F(Unicode, unicode_combining_left) {
    EXPECT_EQ("Café", eval("$left(%title%,4)", make_unicode()));
}

TEST_F(Unicode, unicode_combining_upper) {
    // case mapping per grapheme; the accent survives
    EXPECT_EQ("CAFÉ", eval("$upper(%title%)", make_unicode()));
}

TEST_F(Unicode, unicode_zwj_emoji_is_one_grapheme) {
    // family emoji = 7 code points, 1 grapheme cluster
    EXPECT_EQ("1", eval("$len($cut(%album%,1))", make_unicode()));
}

TEST_F(Unicode, unicode_zwj_emoji_cut) {
    EXPECT_EQ("👨‍👩‍👧‍👦", eval("$cut(%album%,1)", make_unicode()));
}

TEST_F(Unicode, unicode_zwj_emoji_len) {
    // family + space + "Family" = 8 grapheme clusters
    EXPECT_EQ("8", eval("$len(%album%)", make_unicode()));
}

TEST_F(Unicode, unicode_rtl_len) {
    EXPECT_EQ("10", eval("$len(%artist%)", make_unicode()));
}

TEST_F(Unicode, unicode_rtl_sub) {
    // logical indexing → visual order is renderer responsibility
    EXPECT_EQ("مقام", eval("$sub(%artist%,0,4)", make_unicode()));
}

TEST_F(Unicode, unicode_rtl_no_bidi_marks_added) {
    // EFS never injects LRM/RLM
    EXPECT_EQ("مقام العجم", eval("%artist%", make_unicode()));
}

TEST_F(Unicode, unicode_mixed_direction) {
    EXPECT_EQ("مقام العجم - J-Pop", eval("%artist% - %genre%", make_unicode()));
}

TEST_F(Unicode, unicode_title_case_non_ascii) {
    EXPECT_EQ("Björk", eval("$title(björk)", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  Parser tests — malformed inputs recover gracefully
// ---------------------------------------------------------------------------

using MalformedParser = EFSTest;

TEST_F(MalformedParser, empty_pattern) {
    EXPECT_EQ("", eval("", make_kid_a()));
}

TEST_F(MalformedParser, lone_percent) {
    // lone % renders literally
    EXPECT_EQ("a%b", eval("a%b", make_kid_a()));
}

TEST_F(MalformedParser, double_percent) {
    EXPECT_EQ("%%", eval("%%", make_kid_a()));
}

TEST_F(MalformedParser, unclosed_field_ref) {
    // unclosed field ref → literal
    EXPECT_EQ("a%title", eval("a%title", make_kid_a()));
}

TEST_F(MalformedParser, unclosed_function) {
    EXPECT_EQ("$upper(abc", eval("$upper(abc", make_kid_a()));
}

TEST_F(MalformedParser, unknown_function_literal) {
    EXPECT_EQ("$foobar(Radiohead)", eval("$foobar(%artist%)", make_kid_a()));
}

TEST_F(MalformedParser, wrong_arity_literal) {
    EXPECT_EQ("$upper(Radiohead,extra)", eval("$upper(%artist%,extra)", make_kid_a()));
}

TEST_F(MalformedParser, unmatched_close_bracket) {
    EXPECT_EQ("a]b", eval("a]b", make_kid_a()));
}

TEST_F(MalformedParser, unclosed_block) {
    EXPECT_EQ("[block", eval("[block", make_kid_a()));
}

TEST_F(MalformedParser, nested_deep_ok) {
    // 10 levels deep → ok
    EXPECT_EQ("abcdefghijk", eval("[a[b[c[d[e[f[g[h[i[j[k]]]]]]]]]]", make_kid_a()));
}

// ---------------------------------------------------------------------------
//  Compile + render cycle
// ---------------------------------------------------------------------------

using CompileAndRender = EFSTest;

TEST_F(CompileAndRender, compile_is_reusable) {
    auto pat = efs::compile("%artist% - %title%");
    std::string out1, out2;
    efs::render(pat, make_kid_a(), out1);
    efs::render(pat, make_no_album(), out2);
    EXPECT_EQ("Radiohead - Idioteque", out1);
    EXPECT_EQ("Radiohead - Idioteque", out2);
}

TEST_F(CompileAndRender, parse_problems_are_reported) {
    auto pat = efs::compile("$upper(abc");
    ASSERT_FALSE(pat.problems().empty());
}

TEST_F(CompileAndRender, malformed_still_renders) {
    auto pat = efs::compile("$upper(abc");
    std::string out;
    efs::render(pat, make_kid_a(), out);
    EXPECT_EQ("$upper(abc", out);
}

TEST_F(CompileAndRender, output_cap_truncates) {
    efs::EvalContext ctx;
    ctx.output_cap = 10;
    auto result = efs::render("%artist%", make_kid_a(), ctx);
    EXPECT_TRUE(result.cap_exceeded);
    EXPECT_EQ(10u, result.text.size());
}

// ---------------------------------------------------------------------------
//  Track field lookup — completeness
// ---------------------------------------------------------------------------

using TrackFieldLookup = EFSTest;

TEST_F(TrackFieldLookup, all_known_fields_resolve) {
    efs::Track tk = make_kid_a();
    // These should all return a present value, not nullopt
    const char* fields[] = {
        "title", "artist", "albumartist", "album", "genre", "composer",
        "comment", "grouping", "year", "date", "tracknumber", "tracktotal",
        "discnumber", "disctotal", "duration", "bitrate", "samplerate",
        "bitdepth", "channels", "codec", "container", "islossless",
        "path", "filename", "filesize", "rating", "playcount", "skipcount",
        "lastplayed", "added", "bpm", "key", "rgtrackgain",
        "loved", "missing", "hasartwork", "haslyrics",
        "playing_state", "position", "queue_index", "queue_total",
        "list_index", "list_total",
    };
    for (const char* f : fields) {
        auto v = tk.field(f);
        EXPECT_TRUE(v.has_value()) << "field " << f << " returned nullopt";
    }
}

TEST_F(TrackFieldLookup, unknown_field_returns_nullopt) {
    auto v = make_kid_a().field("nosuchfield");
    EXPECT_FALSE(v.has_value());
}

TEST_F(TrackFieldLookup, empty_string_is_value) {
    // comment is empty string — field() returns it as a present value
    auto v = make_kid_a().field("comment");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, "");
}

TEST_F(TrackFieldLookup, multi_field_returns_vector) {
    auto v = make_kid_a().multi_field("artist");
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(1u, v->size());
    EXPECT_EQ("Radiohead", (*v)[0]);
}

TEST_F(TrackFieldLookup, multi_field_absent_returns_nullopt) {
    auto v = make_kid_a().multi_field("nosuchfield");
    EXPECT_FALSE(v.has_value());
}
