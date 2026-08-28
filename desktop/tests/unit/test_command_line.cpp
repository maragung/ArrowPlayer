// SPDX-License-Identifier: MPL-2.0
#include "app/app_info.hpp"
#include "app/command_line.hpp"

#include <gtest/gtest.h>

using arrow::ErrorCode;
using arrow::app::AppInfo;
using arrow::app::CommandLine;
using arrow::app::parse_command_line;
using arrow::app::SinkChoice;

namespace {
struct Args {
    std::vector<std::string> values;
    std::vector<char*> pointers;

    explicit Args(std::initializer_list<const char*> input) {
        for (const char* value : input) values.emplace_back(value);
        for (auto& value : values) pointers.push_back(value.data());
    }
};
}  // namespace

TEST(CommandLine, ParsesPlaybackAndSink) {
    Args args{"arrow-player", "--play", "song.wav", "--sink", "null"};
    const auto parsed =
        parse_command_line(static_cast<int>(args.pointers.size()), args.pointers.data());
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed->play);
    EXPECT_EQ(parsed->path, "song.wav");
    EXPECT_EQ(parsed->sink, SinkChoice::Null);
}

TEST(CommandLine, ParsesHelp) {
    Args args{"arrow-player", "--help"};
    const auto parsed =
        parse_command_line(static_cast<int>(args.pointers.size()), args.pointers.data());
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed->help);
    EXPECT_FALSE(parsed->play);
}

TEST(CommandLine, RejectsUnknownAndIncompleteOptions) {
    Args unknown{"arrow-player", "--wat"};
    auto parsed =
        parse_command_line(static_cast<int>(unknown.pointers.size()), unknown.pointers.data());
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code(), ErrorCode::InvalidArgument);

    Args incomplete{"arrow-player", "--play"};
    parsed = parse_command_line(static_cast<int>(incomplete.pointers.size()),
                                incomplete.pointers.data());
    EXPECT_FALSE(parsed);
}

TEST(CommandLine, RejectsConflictingAndOrphanSinkOptions) {
    Args conflict{"arrow-player", "--help", "--play", "song.wav"};
    auto parsed = parse_command_line(static_cast<int>(conflict.pointers.size()),
                                     conflict.pointers.data());
    EXPECT_FALSE(parsed);

    Args orphan{"arrow-player", "--sink", "null"};
    parsed =
        parse_command_line(static_cast<int>(orphan.pointers.size()), orphan.pointers.data());
    EXPECT_FALSE(parsed);
}

TEST(CommandLine, ParsesVersionFlag) {
    // --version must set the flag, leave every other option at its default,
    // and not be rejected. The help and sink branches are independent — this
    // test is the only one that exercises the new path.
    Args args{"arrow-player", "--version"};
    const auto parsed =
        parse_command_line(static_cast<int>(args.pointers.size()), args.pointers.data());
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed->version);
    EXPECT_FALSE(parsed->help);
    EXPECT_FALSE(parsed->play);
    EXPECT_EQ(parsed->sink, SinkChoice::Automatic);
    EXPECT_TRUE(parsed->path.empty());

    // The short form is the same flag, not a separate one. If --version and -v
    // ever drift, the failure mode here is exactly the one that matters:
    // `git shortlog -v` would already be too late.
    Args short_form{"arrow-player", "-v"};
    const auto short_parsed = parse_command_line(
        static_cast<int>(short_form.pointers.size()), short_form.pointers.data());
    ASSERT_TRUE(short_parsed);
    EXPECT_TRUE(short_parsed->version);
}

TEST(CommandLine, VersionOutputReflectsConfiguredVersion) {
    // What `arrow-player --version` will print. The CLI's printed string
    // asserts the same way: the configured version is present. Anything more
    // (exact SHA, dirty suffix) is a string-format regression caught by
    // Qt-off-screen suite, not here.
    const AppInfo info = AppInfo::current();
    const std::string printed = info.to_about_text();
    EXPECT_NE(printed.find(info.version), std::string::npos)
        << "CLI version output does not include the configured version ("
        << std::string{info.version} << "); got:\n" << printed;
    EXPECT_NE(printed.find("0.1.0"), std::string::npos)
        << "version.txt declares 0.1.0; CLI must echo that exactly. Got:\n"
        << printed;
}

TEST(CommandLine, VersionCannotCombineWithOtherOptions) {
    // Same defensive rule the parser already applies to --help. A query for
    // the version is either an interactive request (alone) or it is a
    // mistake; either way, the user did not ask for two things.
    Args with_play{"arrow-player", "--version", "--play", "song.wav"};
    auto parsed = parse_command_line(static_cast<int>(with_play.pointers.size()),
                                     with_play.pointers.data());
    ASSERT_FALSE(parsed);
    EXPECT_EQ(parsed.error().code(), ErrorCode::InvalidArgument);

    Args with_help{"arrow-player", "--version", "--help"};
    parsed = parse_command_line(static_cast<int>(with_help.pointers.size()),
                                with_help.pointers.data());
    EXPECT_FALSE(parsed);
}
