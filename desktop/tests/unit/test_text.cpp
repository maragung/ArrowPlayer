// SPDX-License-Identifier: MPL-2.0
// Tests for core/text.hpp — spec §9.2.3 (sort keys), §9.2.4 (Unicode),
// §23.2 (mandatory unit coverage), REQ-LIB-029, REQ-LIB-033, REQ-PLS-013.

#include "core/text.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace eclipse::text;

// ===========================================================================
//  UTF-8 — REQ-LIB-033
// ===========================================================================

TEST(Utf8, DecodesAsciiAndMultibyte) {
    std::size_t pos = 0;
    EXPECT_EQ(decode_utf8("A", pos), U'A');
    EXPECT_EQ(pos, 1u);

    pos = 0;
    EXPECT_EQ(decode_utf8("\u00E9", pos), 0x00E9u);   // e-acute, 2 bytes
    EXPECT_EQ(pos, 2u);

    pos = 0;
    EXPECT_EQ(decode_utf8("\u20AC", pos), 0x20ACu);   // euro sign, 3 bytes
    EXPECT_EQ(pos, 3u);

    pos = 0;
    EXPECT_EQ(decode_utf8("\U0001F600", pos), 0x1F600u);  // emoji, 4 bytes
    EXPECT_EQ(pos, 4u);
}

TEST(Utf8, MalformedInputAlwaysMakesProgress) {
    // Hostile input must never cause an infinite loop. Every rejected byte
    // advances the cursor by exactly one.
    const std::string bad[] = {
        "\x80",              // lone continuation
        "\xC0\xAF",          // overlong two-byte
        "\xE0\x80\xAF",      // overlong three-byte
        "\xED\xA0\x80",      // UTF-16 surrogate
        "\xF5\x80\x80\x80",  // beyond U+10FFFF
        "\xC2",              // truncated
        "\xE2\x82",          // truncated
        "\xFF\xFE",          // invalid leads
    };
    for (const auto& s : bad) {
        std::size_t pos = 0;
        std::size_t guard = 0;
        while (pos < s.size() && guard++ < 100) {
            const std::size_t before = pos;
            (void)decode_utf8(s, pos);
            ASSERT_GT(pos, before) << "cursor stalled on malformed input";
        }
        EXPECT_LT(guard, 100u);
        EXPECT_FALSE(is_valid_utf8(s)) << "should reject: " << s;
    }
}

TEST(Utf8, ValidatesGoodInput) {
    EXPECT_TRUE(is_valid_utf8(""));
    EXPECT_TRUE(is_valid_utf8("plain ascii"));
    EXPECT_TRUE(is_valid_utf8("Bj\u00F6rk"));
    EXPECT_TRUE(is_valid_utf8("\u65E5\u672C\u8A9E"));          // Japanese
    EXPECT_TRUE(is_valid_utf8("\u0627\u0644\u0639\u0631\u0628"));  // Arabic
    EXPECT_TRUE(is_valid_utf8("\U0001F3B5 music"));            // emoji
}

TEST(Utf8, RoundTripsEncodeDecode) {
    for (char32_t cp : {U'A', char32_t{0x7F}, char32_t{0x80}, char32_t{0x7FF},
                        char32_t{0x800}, char32_t{0xFFFD}, char32_t{0x10000},
                        char32_t{0x10FFFF}}) {
        std::string encoded;
        encode_utf8(cp, encoded);
        std::size_t pos = 0;
        EXPECT_EQ(decode_utf8(encoded, pos), cp);
        EXPECT_EQ(pos, encoded.size());
    }
}

TEST(Utf8, LengthAndSubstrRespectCodepointBoundaries) {
    const std::string s = "a\u00E9\u20AC\U0001F600z";  // 1+2+3+4+1 bytes, 5 codepoints
    EXPECT_EQ(s.size(), 11u);
    EXPECT_EQ(utf8_length(s), 5u);
    EXPECT_EQ(utf8_substr(s, 0, 1), "a");
    EXPECT_EQ(utf8_substr(s, 1, 1), "\u00E9");
    EXPECT_EQ(utf8_substr(s, 3, 1), "\U0001F600");
    EXPECT_EQ(utf8_substr(s, 4), "z");
    EXPECT_EQ(utf8_substr(s, 99), "");
}

TEST(Utf8, SanitizeReplacesMalformedSequences) {
    const std::string out = sanitize_utf8("ok\x80here");
    EXPECT_TRUE(is_valid_utf8(out));
    EXPECT_NE(out.find("\uFFFD"), std::string::npos);
}

// ===========================================================================
//  Case and diacritics
// ===========================================================================

TEST(Case, AsciiAndLatin1) {
    EXPECT_EQ(to_lower("HELLO World"), "hello world");
    EXPECT_EQ(to_upper("hello world"), "HELLO WORLD");
    EXPECT_EQ(to_lower("BJ\u00D6RK"), "bj\u00F6rk");
    EXPECT_EQ(to_upper("bj\u00F6rk"), "BJ\u00D6RK");
}

TEST(Case, CyrillicAndGreek) {
    EXPECT_EQ(to_lower("\u041F\u0420\u0418\u0412\u0415\u0422"), "\u043F\u0440\u0438\u0432\u0435\u0442");
    EXPECT_EQ(to_upper("\u03B1\u03B2\u03B3"), "\u0391\u0392\u0393");
}

TEST(Case, TitleCaseHandlesWordBoundaries) {
    EXPECT_EQ(to_title("hello world"), "Hello World");
    EXPECT_EQ(to_title("THE BEATLES"), "The Beatles");
    EXPECT_EQ(to_title("a-ha"), "A-Ha");
    EXPECT_EQ(to_title("(live version)"), "(Live Version)");
}

TEST(Diacritics, StripsLatinAccents) {
    EXPECT_EQ(strip_diacritic(0x00E9u), U'e');   // e-acute
    EXPECT_EQ(strip_diacritic(0x00F6u), U'o');   // o-umlaut
    EXPECT_EQ(strip_diacritic(0x00E7u), U'c');   // c-cedilla
    EXPECT_EQ(strip_diacritic(0x0161u), U's');   // s-caron
    EXPECT_EQ(strip_diacritic(0x0141u), U'L');   // L-stroke
    EXPECT_EQ(strip_diacritic(U'a'), U'a');      // unchanged
}

// ===========================================================================
//  Sort keys — REQ-LIB-029, REQ-LIB-030
// ===========================================================================

TEST(SortKey, StripsLeadingArticleEnglish) {
    EXPECT_EQ(sort_key("The Beatles", "en"), "beatles");
    EXPECT_EQ(sort_key("A Perfect Circle", "en"), "perfect circle");
    EXPECT_EQ(sort_key("An Awesome Wave", "en"), "awesome wave");
}

TEST(SortKey, DoesNotStripArticleThatIsPartOfAWord) {
    // "Theatre" must not become "atre" — the article needs a word boundary.
    EXPECT_EQ(sort_key("Theatre", "en"), "theatre");
    EXPECT_EQ(sort_key("Anderson", "en"), "anderson");
    EXPECT_EQ(sort_key("Amy Winehouse", "en"), "amy winehouse");
}

TEST(SortKey, FoldsCaseAndDiacritics) {
    EXPECT_EQ(sort_key("Bj\u00F6rk", "en"), "bjork");
    EXPECT_EQ(sort_key("M\u00F6tley Cr\u00FCe", "en"), "motley crue");
    EXPECT_EQ(sort_key("Sigur R\u00F3s", "en"), "sigur ros");
    EXPECT_EQ(sort_key("BEYONC\u00C9", "en"), "beyonce");
}

TEST(SortKey, CollapsesWhitespaceAndTrims) {
    EXPECT_EQ(sort_key("  The    Who  ", "en"), "who");
    EXPECT_EQ(sort_key("Pink\t\tFloyd", "en"), "pink floyd");
}

TEST(SortKey, LocaleSpecificArticles) {
    EXPECT_EQ(sort_key("Die \u00C4rzte", "de"), "arzte");
    EXPECT_EQ(sort_key("Les Rita Mitsouko", "fr"), "rita mitsouko");
    EXPECT_EQ(sort_key("El Ultimo", "es"), "ultimo");
    // Indonesian has no articles: the string must survive intact.
    EXPECT_EQ(sort_key("The Panasdalam", "id"), "the panasdalam");
}

TEST(SortKey, GivesCorrectOrderingForRealArtistNames) {
    // The end goal: "The Beatles" sorts under B, between Beach and Beck.
    std::vector<std::string> names = {"The Beatles", "Beck", "\u00C1lvaro", "Beach House"};
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
        return sort_key(a, "en") < sort_key(b, "en");
    });
    EXPECT_EQ(names, (std::vector<std::string>{"\u00C1lvaro", "Beach House", "The Beatles", "Beck"}));
}

// ===========================================================================
//  Multi-value splitting — REQ-LIB-028
// ===========================================================================

TEST(SplitMulti, UsesDefaultSeparators) {
    const std::vector<std::string> seps = {";", " / ", " feat. ", " ft. ", " & "};
    EXPECT_EQ(split_multi("A;B;C", seps), (std::vector<std::string>{"A", "B", "C"}));
    EXPECT_EQ(split_multi("A / B", seps), (std::vector<std::string>{"A", "B"}));
    EXPECT_EQ(split_multi("Artist feat. Guest", seps),
              (std::vector<std::string>{"Artist", "Guest"}));
}

TEST(SplitMulti, CommaIsNotASeparatorByDefault) {
    // REQ-LIB-028: a comma default would destroy "Earth, Wind & Fire".
    const std::vector<std::string> seps = {";", " / ", " feat. "};
    EXPECT_EQ(split_multi("Earth, Wind & Fire", seps),
              (std::vector<std::string>{"Earth, Wind & Fire"}));
}

TEST(SplitMulti, PrefersLongestSeparator) {
    const std::vector<std::string> seps = {"/", " / "};
    EXPECT_EQ(split_multi("A / B", seps), (std::vector<std::string>{"A", "B"}));
}

TEST(SplitMulti, DropsEmptyFragments) {
    const std::vector<std::string> seps = {";"};
    EXPECT_EQ(split_multi(";A;;B;", seps), (std::vector<std::string>{"A", "B"}));
}

// ===========================================================================
//  Numbers — hostile input must never wrap
// ===========================================================================

TEST(ParseInt, AcceptsValidAndRejectsGarbage) {
    std::int64_t v = 0;
    EXPECT_TRUE(parse_int("0", v));       EXPECT_EQ(v, 0);
    EXPECT_TRUE(parse_int("42", v));      EXPECT_EQ(v, 42);
    EXPECT_TRUE(parse_int("-17", v));     EXPECT_EQ(v, -17);
    EXPECT_TRUE(parse_int("  8  ", v));   EXPECT_EQ(v, 8);

    EXPECT_FALSE(parse_int("", v));
    EXPECT_FALSE(parse_int("abc", v));
    EXPECT_FALSE(parse_int("12abc", v));
    EXPECT_FALSE(parse_int("1.5", v));
    EXPECT_FALSE(parse_int("-", v));
    EXPECT_FALSE(parse_int("+", v));
}

TEST(ParseInt, RejectsOverflowRatherThanWrapping) {
    std::int64_t v = 0;
    EXPECT_TRUE(parse_int("9223372036854775807", v));
    EXPECT_EQ(v, 9223372036854775807LL);
    EXPECT_FALSE(parse_int("9223372036854775808", v));
    EXPECT_FALSE(parse_int("99999999999999999999999999", v));
}

TEST(ParseHex, ForITunSmpbStyleFields) {
    std::uint64_t v = 0;
    EXPECT_TRUE(parse_hex("00000840", v));  EXPECT_EQ(v, 0x840u);
    EXPECT_TRUE(parse_hex("1C0", v));       EXPECT_EQ(v, 0x1C0u);
    EXPECT_TRUE(parse_hex("ffffFFFF", v));  EXPECT_EQ(v, 0xFFFFFFFFu);
    EXPECT_FALSE(parse_hex("00 40", v));
    EXPECT_FALSE(parse_hex("xyz", v));
    EXPECT_FALSE(parse_hex("", v));
    EXPECT_FALSE(parse_hex("00000000000000000", v));  // > 16 digits
}

// ===========================================================================
//  Glob — REQ-PLS-013 (regex is deliberately excluded)
// ===========================================================================

TEST(Glob, MatchesWildcards) {
    EXPECT_TRUE(glob_match("*", "anything"));
    EXPECT_TRUE(glob_match("*.mp3", "song.mp3"));
    EXPECT_TRUE(glob_match("track?.flac", "track1.flac"));
    EXPECT_FALSE(glob_match("track?.flac", "track12.flac"));
    EXPECT_TRUE(glob_match("*live*", "a live recording"));
    EXPECT_FALSE(glob_match("*.mp3", "song.flac"));
}

TEST(Glob, CharacterClasses) {
    EXPECT_TRUE(glob_match("track[0-9].mp3", "track7.mp3"));
    EXPECT_FALSE(glob_match("track[0-9].mp3", "trackX.mp3"));
    EXPECT_TRUE(glob_match("[!x]yz", "ayz"));
    EXPECT_FALSE(glob_match("[!x]yz", "xyz"));
}

TEST(Glob, CaseInsensitiveByDefault) {
    EXPECT_TRUE(glob_match("*.MP3", "song.mp3"));
    EXPECT_FALSE(glob_match("*.MP3", "song.mp3", /*case_sensitive=*/true));
}

TEST(Glob, PathologicalPatternTerminates) {
    // The iterative matcher must not blow up on many wildcards, which is the
    // exact failure mode regex would have (catastrophic backtracking).
    const std::string pattern(64, '*');
    const std::string subject(2000, 'a');
    EXPECT_TRUE(glob_match(pattern, subject));

    std::string alt;
    for (int i = 0; i < 40; ++i) alt += "a*";
    EXPECT_TRUE(glob_match(alt, std::string(500, 'a')));
}

// ===========================================================================
//  Edit distance — §9.5 fuzzy search stage
// ===========================================================================

TEST(EditDistance, BasicOperations) {
    EXPECT_EQ(edit_distance("kitten", "sitting", 10), 3u);
    EXPECT_EQ(edit_distance("", "abc", 10), 3u);
    EXPECT_EQ(edit_distance("same", "same", 10), 0u);
    EXPECT_EQ(edit_distance("ab", "ba", 10), 1u);   // transposition
}

TEST(EditDistance, RespectsBound) {
    EXPECT_GT(edit_distance("completely", "different", 2), 2u);
    EXPECT_EQ(edit_distance("radiohed", "radiohead", 2), 1u);
}

// ===========================================================================
//  Path safety — REQ-THM-018, REQ-SEC-008
// ===========================================================================

TEST(PathSafety, RejectsTraversalAndAbsolutePaths) {
    const std::string unsafe[] = {
        "../etc/passwd",
        "a/../../b",
        "..",
        "/etc/passwd",
        "\\windows\\system32",
        "C:/Windows",
        "c:\\windows",
        "\\\\server\\share",
        "images/../../secret",
        std::string("with\0nul", 8),   // explicit length: NUL is interior
        "",
    };
    for (const auto& p : unsafe) {
        EXPECT_TRUE(is_unsafe_relative_path(p)) << "should reject: " << p;
    }
}

TEST(PathSafety, AcceptsLegitimatePackagePaths) {
    const char* safe[] = {
        "theme.json",
        "images/background.png",
        "layout/now-playing.eclayout",
        "icons/play.svg",
        "fonts/Inter-Regular.ttf",
        "a/b/c/d.png",
    };
    for (const char* p : safe) {
        EXPECT_FALSE(is_unsafe_relative_path(p)) << "should accept: " << p;
    }
}

TEST(PathSafety, RejectsOverlongPaths) {
    EXPECT_TRUE(is_unsafe_relative_path(std::string(201, 'a')));
    EXPECT_FALSE(is_unsafe_relative_path(std::string(199, 'a')));
}

TEST(PathSafety, RejectsControlCharacters) {
    EXPECT_TRUE(is_unsafe_relative_path(std::string("a\x01b")));
    EXPECT_TRUE(is_unsafe_relative_path(std::string("a\x7F" "b")));
    EXPECT_TRUE(is_unsafe_relative_path(std::string("a\nb")));
}

TEST(Normalize, ResolvesDotSegments) {
    std::string out;
    EXPECT_TRUE(normalize_relative_path("a/./b", out));      EXPECT_EQ(out, "a/b");
    EXPECT_TRUE(normalize_relative_path("a/b/../c", out));   EXPECT_EQ(out, "a/c");
    EXPECT_TRUE(normalize_relative_path("a\\b", out));       EXPECT_EQ(out, "a/b");
    EXPECT_TRUE(normalize_relative_path("a//b", out));       EXPECT_EQ(out, "a/b");
}

TEST(Normalize, RefusesToEscapeRoot) {
    std::string out;
    EXPECT_FALSE(normalize_relative_path("../x", out));
    EXPECT_FALSE(normalize_relative_path("a/../../x", out));
    EXPECT_FALSE(normalize_relative_path(".", out));
}

// ===========================================================================
//  Misc helpers
// ===========================================================================

TEST(Trim, HandlesUnicodeSpaces) {
    EXPECT_EQ(trim("  hello  "), "hello");
    EXPECT_EQ(trim("\t\nhello\r\n"), "hello");
    EXPECT_EQ(trim("\u00A0hello\u00A0"), "hello");   // non-breaking space
    EXPECT_EQ(trim(""), "");
    EXPECT_EQ(trim("   "), "");
}

TEST(CaseInsensitiveCompare, Works) {
    EXPECT_TRUE(iequals("Hello", "HELLO"));
    EXPECT_FALSE(iequals("Hello", "Hell"));
    EXPECT_TRUE(istarts_with("Hello World", "hello"));
    EXPECT_TRUE(iends_with("song.MP3", ".mp3"));
    EXPECT_TRUE(icontains("The Dark Side", "dark"));
    EXPECT_FALSE(icontains("abc", "xyz"));
    EXPECT_TRUE(icontains("abc", ""));
}

TEST(ReplaceAll, Works) {
    EXPECT_EQ(replace_all("a-b-c", "-", "+"), "a+b+c");
    EXPECT_EQ(replace_all("aaa", "aa", "b"), "ba");
    EXPECT_EQ(replace_all("abc", "x", "y"), "abc");
    EXPECT_EQ(replace_all("abc", "", "y"), "abc");   // empty needle is a no-op
}

TEST(Split, KeepsOrDropsEmpties) {
    EXPECT_EQ(split("a,b,c", ',').size(), 3u);
    EXPECT_EQ(split("a,,c", ',', true).size(), 3u);
    EXPECT_EQ(split("a,,c", ',', false).size(), 2u);
    EXPECT_EQ(split("", ',', false).size(), 0u);
}
