// SPDX-License-Identifier: MPL-2.0
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "audio/decode/wav_decoder.hpp"

#include <gtest/gtest.h>

namespace {

void write_u16(std::ofstream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 0xffU));
    out.put(static_cast<char>(value >> 8U));
}

void write_u32(std::ofstream& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.put(static_cast<char>(value >> shift));
}

std::filesystem::path make_wav() {
    const auto path = std::filesystem::temp_directory_path() / "eclipse-player-test.wav";
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    write_u32(out, 44);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32(out, 16);
    write_u16(out, 1);
    write_u16(out, 1);
    write_u32(out, 44100);
    write_u32(out, 88200);
    write_u16(out, 2);
    write_u16(out, 16);
    out.write("data", 4);
    write_u32(out, 4);
    write_u16(out, 0);
    write_u16(out, 32767);
    return path;
}

TEST(WavDecoder, ReadsPcmSamplesAndSeeks) {
    const auto path = make_wav();
    eclipse::audio::WavDecoder decoder;
    const auto info = decoder.open(path);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->total_frames, 2U);
    std::array<float, 2> samples{};
    float* planes[] = {samples.data()};
    const auto read = decoder.read({planes, 1, 2});
    ASSERT_TRUE(read);
    EXPECT_EQ(read.value(), 2U);
    EXPECT_NEAR(samples[0], 0.0F, 0.001F);
    EXPECT_NEAR(samples[1], 1.0F, 0.001F);
    ASSERT_TRUE(decoder.seek(1));
    ASSERT_TRUE(decoder.read({planes, 1, 1}));
    EXPECT_NEAR(samples[0], 1.0F, 0.001F);
    // Close the decoder before removing the file: on Windows, the OS keeps
    // a write/delete lock on the open handle and remove() throws.
    decoder.close();
    std::filesystem::remove(path);
}

TEST(WavDecoder, RejectsInvalidStateAndMalformedInput) {
    eclipse::audio::WavDecoder decoder;
    EXPECT_EQ(decoder.read({nullptr, 0, 1}).error().code(), eclipse::ErrorCode::InvalidState);
    const auto path = std::filesystem::temp_directory_path() / "eclipse-player-bad.wav";
    std::ofstream(path, std::ios::binary).write("bad", 3);
    const auto result = decoder.open(path);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code(), eclipse::ErrorCode::CorruptStream);
    std::filesystem::remove(path);
}

}  // namespace
