// SPDX-License-Identifier: MPL-2.0
#include <array>
#include <filesystem>
#include <fstream>

#include "audio/decode/wav_decoder.hpp"
#include "audio/playback_service.hpp"
#include "audio/sink/null_sink.hpp"

#include <gtest/gtest.h>

namespace {

void put16(std::ofstream& out, unsigned value) {
    out.put(static_cast<char>(value & 0xffU));
    out.put(static_cast<char>((value >> 8U) & 0xffU));
}

void put32(std::ofstream& out, unsigned value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.put(static_cast<char>((value >> shift) & 0xffU));
}

TEST(PlaybackService, PlaysWavToInjectedSink) {
    const auto path = std::filesystem::temp_directory_path() / "arrow-player-service.wav";
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    put32(out, 42);
    out.write("WAVEfmt ", 8);
    put32(out, 16);
    put16(out, 1);
    put16(out, 1);
    put32(out, 44100);
    put32(out, 88200);
    put16(out, 2);
    put16(out, 16);
    out.write("data", 4);
    put32(out, 2);
    put16(out, 16384);
    out.close();
    arrow::audio::WavDecoder decoder;
    arrow::audio::NullSink sink;
    arrow::audio::PlaybackService service{decoder, sink, 8};
    ASSERT_TRUE(service.play_file(path, 4));
    EXPECT_EQ(sink.written_frames(), 1U);
    std::filesystem::remove(path);
}

TEST(PlaybackService, RejectsZeroChunkSize) {
    arrow::audio::WavDecoder decoder;
    arrow::audio::NullSink sink;
    arrow::audio::PlaybackService service{decoder, sink, 8};
    const auto result = service.play_file("unused.wav", 0);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), arrow::ErrorCode::InvalidArgument);
}

}  // namespace
