// SPDX-License-Identifier: MPL-2.0
#include <gtest/gtest.h>

#include <array>

#include "audio/sink/null_sink.hpp"

namespace {

constexpr eclipse::audio::PcmFormat kFormat{44100, 2, 32};

TEST(NullSink, RequiresOpenAndStart) {
    eclipse::audio::NullSink sink;
    std::array<float, 1> samples{};
    float* planes[] = {samples.data(), samples.data()};
    EXPECT_EQ(sink.write({planes, 2, 1}).error().code(), eclipse::ErrorCode::InvalidState);
    ASSERT_TRUE(sink.open({kFormat, false, 256}));
    EXPECT_EQ(sink.write({planes, 2, 1}).error().code(), eclipse::ErrorCode::InvalidState);
    ASSERT_TRUE(sink.start());
    ASSERT_TRUE(sink.write({planes, 2, 1}));
    EXPECT_EQ(sink.written_frames(), 1U);
    sink.stop();
    sink.close();
}

TEST(NullSink, RejectsInvalidConfigurationAndBuffers) {
    eclipse::audio::NullSink sink;
    const auto invalid_format = sink.open({{}, false, 256});
    ASSERT_FALSE(invalid_format);
    EXPECT_EQ(invalid_format.error().code(), eclipse::ErrorCode::InvalidArgument);
    ASSERT_TRUE(sink.open({kFormat, false, 256}));
    ASSERT_TRUE(sink.start());
    EXPECT_EQ(sink.write({nullptr, 2, 1}).error().code(), eclipse::ErrorCode::InvalidArgument);
    const auto empty = sink.write({nullptr, 2, 0});
    ASSERT_TRUE(empty);
}

}  // namespace
