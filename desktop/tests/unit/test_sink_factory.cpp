// SPDX-License-Identifier: MPL-2.0
#include "audio/sink/null_sink.hpp"
#include "audio/sink/sink_factory.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SinkFactory, ExplicitNullCreatesNullSink) {
    const auto result =
        eclipse::audio::make_sink({.preference = eclipse::audio::SinkPreference::Null});
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value()->device_name(), "null");
}

TEST(SinkFactory, AutomaticProvidesAUsableSink) {
    const auto result = eclipse::audio::make_sink();
    ASSERT_TRUE(result);
    EXPECT_FALSE(result.value()->device_name().empty());
}

#if !defined(ECLIPSE_HAVE_ALSA)
TEST(SinkFactory, ExplicitAlsaReportsUnavailable) {
    const auto result =
        eclipse::audio::make_sink({.preference = eclipse::audio::SinkPreference::Alsa});
    EXPECT_FALSE(result);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), eclipse::ErrorCode::DeviceNotFound);
}
#endif

}  // namespace
