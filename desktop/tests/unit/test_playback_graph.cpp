// SPDX-License-Identifier: MPL-2.0
#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "audio/graph/playback_graph.hpp"

#include <gtest/gtest.h>

namespace eclipse::audio {
namespace {

class FakeDecoder final : public IDecoder {
  public:
    Result<StreamInfo> open(const std::filesystem::path&) override {
        opened = true;
        return StreamInfo{PcmFormat{48000, 1, 32}, 4, 0, 0};
    }

    Result<std::size_t> read(const PlanarFrames destination) override {
        if (!opened) {
            return err(ErrorCode::InvalidState, "decoder closed");
        }
        // The IDecoder contract is uint64_t; the underlying std::array
        // is indexed by size_t. The implicit conversion from position
        // to size_t is correct on every platform we test because the
        // fake only ever holds 4 samples. An explicit cast would be
        // -Wuseless-cast on LP64 where the two widths coincide, so the
        // narrowing is left implicit here.
        const std::size_t pos = position;
        const auto count = std::min<std::size_t>(destination.frames,
                                                  samples.size() - pos);
        for (std::size_t i = 0; i < count; ++i) {
            destination.planes[0][i] = samples[pos + i];
        }
        position += count;
        return count;
    }

    Status seek(std::uint64_t frame) override {
        if (frame > samples.size()) {
            return err(ErrorCode::OutOfRange, "seek outside stream");
        }
        position = frame;
        return ok();
    }

    void close() noexcept override { opened = false; }

    std::array<float, 4> samples{1.0F, 2.0F, 3.0F, 4.0F};
    // The position matches the IDecoder contract's uint64_t so seek/read
    // arithmetic is one consistent width and the type matches the
    // interface even on platforms where size_t is wider.
    std::uint64_t position{0};
    bool opened{false};
};

class FakeSink final : public IAudioSink {
  public:
    Result<SinkConfig> open(const SinkConfig& requested) override { return requested; }

    Status start() override { return ok(); }

    void stop() noexcept override {}

    void close() noexcept override { closed = true; }

    Status write(const PlanarFrames frames) noexcept override {
        for (std::size_t i = 0; i < frames.frames; ++i) {
            received.push_back(frames.planes[0][i]);
        }
        return ok();
    }

    std::string_view device_name() const noexcept override { return "fake"; }

    std::vector<float> received;
    bool closed{false};
};

TEST(PlaybackGraph, ProducesAndConsumesAllFrames) {
    FakeDecoder decoder;
    FakeSink sink;
    PlaybackGraph graph{decoder, sink, 4};

    ASSERT_TRUE(graph.open("track.raw"));
    ASSERT_TRUE(graph.produce(4));
    EXPECT_EQ(graph.queued_frames(), 4U);
    ASSERT_TRUE(graph.consume(4));
    EXPECT_EQ(sink.received, (std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
    ASSERT_TRUE(graph.produce(4));
    EXPECT_TRUE(graph.at_end());
    graph.close();
    EXPECT_TRUE(sink.closed);
}

TEST(PlaybackGraph, ReportsUnderrunWhenEmpty) {
    FakeDecoder decoder;
    FakeSink sink;
    PlaybackGraph graph{decoder, sink, 2};
    ASSERT_TRUE(graph.open("track.raw"));
    const auto result = graph.consume(1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::BufferUnderrun);
}

TEST(PlaybackGraph, ReportsInvalidStateBeforeOpen) {
    FakeDecoder decoder;
    FakeSink sink;
    PlaybackGraph graph{decoder, sink, 2};
    const auto result = graph.produce(1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::InvalidState);
}

}  // namespace
}  // namespace eclipse::audio
