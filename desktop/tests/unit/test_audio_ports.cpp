// SPDX-License-Identifier: MPL-2.0
#include <array>
#include <cstddef>

#include "audio/graph/spsc_ring.hpp"
#include "audio/ports/audio_types.hpp"

#include <gtest/gtest.h>

namespace eclipse::audio {
namespace {

TEST(PcmFormat, AcceptsSupportedFormat) {
    const PcmFormat format{48000, 2, 32};
    EXPECT_TRUE(format.validate());
}

TEST(PcmFormat, RejectsInvalidValues) {
    const PcmFormat zero_rate{0, 2, 32};
    const PcmFormat zero_channels{48000, 0, 32};
    const PcmFormat bad_depth{48000, 2, 20};
    EXPECT_FALSE(zero_rate.validate());
    EXPECT_FALSE(zero_channels.validate());
    EXPECT_FALSE(bad_depth.validate());
}

TEST(PlanarFrames, EmptyFrameBlockMayHaveNoPlanes) {
    const PlanarFrames empty{nullptr, 0, 0};
    EXPECT_TRUE(empty.valid());
}

TEST(PlanarFrames, RejectsNullPlaneForNonEmptyBlock) {
    float* planes[] = {nullptr, nullptr};
    const PlanarFrames invalid{planes, 2, 4};
    EXPECT_FALSE(invalid.valid());
}

TEST(SpscPcmRing, PreservesPlanarSamplesAndBoundsPush) {
    SpscPcmRing ring{2, 2};
    std::array<float, 3> left{1.0F, 2.0F, 3.0F};
    std::array<float, 3> right{4.0F, 5.0F, 6.0F};
    float* input[] = {left.data(), right.data()};

    EXPECT_EQ(ring.push(PlanarFrames{input, 2, 3}), 2U);
    EXPECT_EQ(ring.available_read(), 2U);

    std::array<float, 2> out_left{};
    std::array<float, 2> out_right{};
    float* output[] = {out_left.data(), out_right.data()};
    EXPECT_EQ(ring.pop(PlanarFrames{output, 2, 2}), 2U);
    EXPECT_EQ(out_left, (std::array<float, 2>{1.0F, 2.0F}));
    EXPECT_EQ(out_right, (std::array<float, 2>{4.0F, 5.0F}));
}

TEST(SpscPcmRing, WrapsAround) {
    SpscPcmRing ring{2, 1};
    std::array<float, 2> first{1.0F, 2.0F};
    float* first_plane[] = {first.data()};
    EXPECT_EQ(ring.push(PlanarFrames{first_plane, 1, 2}), 2U);

    std::array<float, 1> consumed{};
    float* consumed_plane[] = {consumed.data()};
    EXPECT_EQ(ring.pop(PlanarFrames{consumed_plane, 1, 1}), 1U);

    std::array<float, 2> second{3.0F, 4.0F};
    float* second_plane[] = {second.data()};
    EXPECT_EQ(ring.push(PlanarFrames{second_plane, 1, 2}), 1U);

    std::array<float, 3> output{};
    float* output_plane[] = {output.data()};
    EXPECT_EQ(ring.pop(PlanarFrames{output_plane, 1, 3}), 2U);
    EXPECT_EQ(output[0], 2.0F);
    EXPECT_EQ(output[1], 3.0F);
}

TEST(SpscPcmRing, RejectsWrongChannelCount) {
    SpscPcmRing ring{4, 2};
    float samples[2] = {1.0F, 2.0F};
    float* planes[] = {samples};
    EXPECT_EQ(ring.push(PlanarFrames{planes, 1, 2}), 0U);
}

}  // namespace
}  // namespace eclipse::audio
