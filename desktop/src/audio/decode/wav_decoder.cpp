// SPDX-License-Identifier: MPL-2.0
#include "audio/decode/wav_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include <vector>

namespace eclipse::audio {
namespace {

std::uint32_t u32(const std::array<char, 4>& b) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[0])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b[3])) << 24);
}

std::uint16_t u16(const char* b) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(b[0])) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(b[1])) << 8);
}

bool read_exact(std::ifstream& input, char* destination, std::streamsize size) {
    input.read(destination, size);
    return input.gcount() == size;
}

}  // namespace

Result<StreamInfo> WavDecoder::open(const std::filesystem::path& path) {
    close();
    input_.open(path, std::ios::binary);
    if (!input_) return err(ErrorCode::FileNotFound, "The audio file could not be opened.");

    std::array<char, 4> riff{};
    std::array<char, 4> wave{};
    std::array<char, 4> chunk{};
    char size_bytes[4]{};
    if (!read_exact(input_, riff.data(), 4) || !read_exact(input_, size_bytes, 4) ||
        !read_exact(input_, wave.data(), 4) || std::string_view{riff.data(), 4} != "RIFF" ||
        std::string_view{wave.data(), 4} != "WAVE") {
        close();
        return err(ErrorCode::CorruptStream, "The WAV header is invalid.");
    }

    bool found_format = false;
    bool found_data = false;
    std::uint32_t sample_rate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    while (input_ && (!found_format || !found_data)) {
        if (!read_exact(input_, chunk.data(), 4) || !read_exact(input_, size_bytes, 4)) break;
        const auto size =
            static_cast<std::uint32_t>(static_cast<unsigned char>(size_bytes[0])) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(size_bytes[1])) << 8U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(size_bytes[2])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(size_bytes[3])) << 24U);
        const auto start = input_.tellg();
        if (std::string_view{chunk.data(), 4} == "fmt ") {
            if (size < 16) break;
            std::array<char, 16> format{};
            if (!read_exact(input_, format.data(), 16) || u16(format.data()) != 1) break;
            channels = u16(format.data() + 2);
            sample_rate = u32(*reinterpret_cast<const std::array<char, 4>*>(format.data() + 4));
            bits = u16(format.data() + 14);
            if (size > 16) input_.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
            found_format = true;
        } else if (std::string_view{chunk.data(), 4} == "data") {
            data_offset_ = static_cast<std::uint64_t>(start);
            data_bytes_ = size;
            input_.seekg(static_cast<std::streamoff>(size), std::ios::cur);
            found_data = true;
        } else {
            input_.seekg(static_cast<std::streamoff>(size + (size & 1U)), std::ios::cur);
        }
    }
    if (!found_format || !found_data || channels == 0 || sample_rate == 0 ||
        (bits != 16 && bits != 24 && bits != 32)) {
        close();
        return err(ErrorCode::UnsupportedFormat, "The WAV format is unsupported.");
    }
    source_channels_ = channels;
    source_bits_ = bits;
    info_.format = PcmFormat{sample_rate, channels, bits};
    info_.total_frames = data_bytes_ / (static_cast<std::uint64_t>(channels) * (bits / 8U));
    position_ = 0;
    input_.clear();
    input_.seekg(static_cast<std::streamoff>(data_offset_), std::ios::beg);
    opened_ = true;
    return info_;
}

Result<std::size_t> WavDecoder::read(const PlanarFrames destination) {
    if (!opened_) return err(ErrorCode::InvalidState, "The decoder is not open.");
    if (!destination.valid() || destination.channels != source_channels_) {
        return err(ErrorCode::InvalidArgument, "The decode buffer is invalid.");
    }
    const auto bytes_per_frame =
        static_cast<std::uint64_t>(source_channels_) * (source_bits_ / 8U);
    const auto remaining = info_.total_frames - std::min(position_, info_.total_frames);
    const auto frames = std::min<std::uint64_t>(destination.frames, remaining);
    if (frames == 0) return std::size_t{0};
    std::vector<char> bytes(frames * bytes_per_frame);
    if (!read_exact(input_, bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        return err(ErrorCode::CorruptStream, "The WAV audio data is truncated.");
    }
    const auto bytes_per_sample = source_bits_ / 8U;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < source_channels_; ++channel) {
            const auto* sample =
                bytes.data() + (frame * source_channels_ + channel) * bytes_per_sample;
            std::int32_t value = 0;
            if (source_bits_ == 16)
                value = static_cast<std::int16_t>(u16(sample));
            else if (source_bits_ == 24)
                value =
                    (static_cast<std::int32_t>(static_cast<unsigned char>(sample[0])) |
                     (static_cast<std::int32_t>(static_cast<unsigned char>(sample[1])) << 8) |
                     (static_cast<std::int32_t>(static_cast<signed char>(sample[2])) << 16));
            else
                value =
                    static_cast<std::int32_t>(static_cast<unsigned char>(sample[0])) |
                    (static_cast<std::int32_t>(static_cast<unsigned char>(sample[1])) << 8) |
                    (static_cast<std::int32_t>(static_cast<unsigned char>(sample[2])) << 16) |
                    (static_cast<std::int32_t>(static_cast<unsigned char>(sample[3])) << 24);
            destination.planes[channel][frame] =
                static_cast<float>(value) /
                static_cast<float>(std::uint64_t{1} << (source_bits_ - 1U));
        }
    }
    position_ += frames;
    return static_cast<std::size_t>(frames);
}

Status WavDecoder::seek(const std::uint64_t frame) {
    if (!opened_ || frame > info_.total_frames)
        return err(ErrorCode::SeekFailed, "The seek position is invalid.");
    const auto bytes_per_frame =
        static_cast<std::uint64_t>(source_channels_) * (source_bits_ / 8U);
    input_.clear();
    input_.seekg(static_cast<std::streamoff>(data_offset_ + frame * bytes_per_frame),
                 std::ios::beg);
    if (!input_) return err(ErrorCode::SeekFailed, "The audio stream could not seek.");
    position_ = frame;
    return ok();
}

void WavDecoder::close() noexcept {
    if (input_.is_open()) input_.close();
    input_.clear();
    opened_ = false;
    position_ = 0;
    data_offset_ = 0;
    data_bytes_ = 0;
}

}  // namespace eclipse::audio
