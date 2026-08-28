// SPDX-License-Identifier: MPL-2.0
#include "app/command_line.hpp"

#include <gtest/gtest.h>

using arrow::ErrorCode;
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
