// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "audio/ports/audio_types.hpp"
#include "core/error.hpp"

namespace arrow::audio {

// ===========================================================================
//  Channel layout
// ===========================================================================

/// Canonical channel layouts — spec §8.3.2.
enum class ChannelLayout : std::uint8_t {
    Mono,       ///< 1 ch — FC
    Stereo,     ///< 2 ch — FL / FR
    Quad,       ///< 4 ch — FL / FR / BL / BR
    Surround51, ///< 5.1 — FL / FR / FC / LFE / BL / BR
    Surround71, ///< 7.1 — FL / FR / FC / LFE / BL / BR / SL / SR
    Hexagonal,  ///< 6 ch — FL / FR / FC / LFE / BL / BR
    Octagonal,  ///< 8 ch
    Unknown,    ///< present but unrecognised
};

[[nodiscard]] constexpr std::string_view to_string(ChannelLayout layout) noexcept;

[[nodiscard]] constexpr std::size_t channel_count(ChannelLayout layout) noexcept;

// ===========================================================================
//  ReplayGain tags
// ===========================================================================

/// ReplayGain 1.0 / 2.0 metadata (spec §8.5).
struct ReplayGainTags final {
    /// Peak amplitude (0.0 = absent).
    double track_peak = 0.0;
    /// Track gain in dB (0.0 = absent).
    double track_gain_db = 0.0;
    /// Album gain in dB (0.0 = absent).
    double album_gain_db = 0.0;
    /// True when the peak field is present.
    bool has_track_peak = false;
    /// True when the track-gain field is present.
    bool has_track_gain = false;
    /// True when the album-gain field is present.
    bool has_album_gain = false;

    /// Returns the preferred gain in dB for playback: album gain if available,
    /// otherwise track gain.  Returns 0.0 if neither is set.
    [[nodiscard]] double preferred_gain_db() const noexcept {
        if (has_album_gain) return album_gain_db;
        if (has_track_gain) return track_gain_db;
        return 0.0;
    }

    /// Returns the peak as a linear amplitude factor.  Returns 1.0 when peak
    /// is absent, so callers that divide by peak never produce infinity.
    [[nodiscard]] double peak_linear() const noexcept {
        return has_track_peak && track_peak > 0.0 ? track_peak : 1.0;
    }
};

// ===========================================================================
//  GaplessInfo — thin wrapper matching the gapless_info.hpp interface
// ===========================================================================

using GaplessSource = int;  // forward-declared; the real enum is in gapless_info.hpp

/// Sample-accurate trim metadata for gapless playback.
struct GaplessInfo final {
    /// Number of encoder-delay / lead-in samples to skip at the start.
    std::uint32_t skip_start_frames = 0;
    /// Number of encoder-padding / lead-out samples to skip at the end.
    std::uint32_t skip_end_frames = 0;
    /// Total valid audio frames after trimming (kUnknownFrames = not known).
    std::uint64_t valid_frames = ~std::uint64_t{0};
    /// Where the trim information came from.
    GaplessSource source = 0;  // GaplessSource::None

    [[nodiscard]] bool supports_sample_exact_splice() const noexcept {
        return source != 0;  // not GaplessSource::None
    }

    /// Frames actually played after applying trim, or kUnknownFrames.
    [[nodiscard]] std::uint64_t playable_frames() const noexcept;
};

// ===========================================================================
//  StreamInfo — complete description of a decoded audio stream
// ===========================================================================

/// Codec identifiers for the decoder's StreamInfo.
enum class CodecId : std::uint8_t {
    Unknown,
    Pcm,
    Mp3,
    Flac,
    Vorbis,
    Opus,
    Aac,
    Alac,
    WavPack,
    Ape,
    Atrac9,
    Gsm,
};

[[nodiscard]] constexpr std::string_view to_string(CodecId codec) noexcept;

/// Container format identifiers.
enum class ContainerId : std::uint8_t {
    Unknown,
    WAV,
    AIFF,
    FLAC,
    Ogg,
    MP4,
    Matroska,
    ASF,
    MPEGTS,
    CDDA,
    WavPack,
    AAC_ADTS,
};

[[nodiscard]] constexpr std::string_view to_string(ContainerId container) noexcept;

/// Complete metadata for an opened audio stream.
struct StreamInfo final {
    /// PCM format of the decoded output (always float32 / planar in this
    /// engine).
    PcmFormat format{44100, 2, 32};

    /// Total frame count from the container / codec header.
    std::uint64_t total_frames = 0;

    /// Number of lead-in samples (encoder delay) to discard at the start.
    std::uint64_t head_trim = 0;

    /// Number of trailing samples (encoder padding) to discard at the end.
    std::uint64_t tail_trim = 0;

    /// Decoded channel arrangement.
    ChannelLayout layout = ChannelLayout::Stereo;

    /// Detected codec.
    CodecId codec = CodecId::Unknown;

    /// Container format the file uses.
    ContainerId container = ContainerId::Unknown;

    /// True when the codec is lossless (FLAC, ALAC, WavPack, PCM, etc.).
    bool is_lossless = false;

    /// True when the stream supports fast seeking without re-opening.
    bool seekable = false;

    /// Gapless / trim metadata.  Empty when no information is available.
    std::optional<GaplessInfo> gapless;

    /// ReplayGain tags found in the file.  All fields default to "absent".
    std::optional<ReplayGainTags> replaygain;

    /// Human-readable codec name for the technical-info panel.
    std::string codec_name;

    /// True when this StreamInfo represents an audible no-op configuration.
    [[nodiscard]] bool is_neutral() const noexcept {
        return format.sample_rate == 44100 && format.channels == 2 &&
               format.bits_per_sample == 32 && head_trim == 0 && tail_trim == 0 &&
               (!gapless || !gapless->supports_sample_exact_splice());
    }
};

// ===========================================================================
//  IDecoder — abstract decoder port
// ===========================================================================

/// Abstract interface for all audio decoders.
///
/// Implementors must be thread-safe: open() / close() / seek() may be called
/// from any thread, but read() is guaranteed to be called from exactly one
/// thread at a time (the decode worker).  The implementation does not need to
/// provide its own locking as long as those constraints are respected.
///
/// RT-SAFE note: read() is called from the decode worker (not the RT audio
/// thread), so it may block.  All other methods may block as well.
///
/// All implementations must be RAII: closing the decoder frees every resource
/// without further caller involvement.
class IDecoder {
  public:
    virtual ~IDecoder() = default;

    /// Opens `path` and reads its headers.  Returns stream metadata on success.
    /// The caller may call read() and seek() after a successful open().
    ///
    /// \param path  Path to the audio file.
    /// \return StreamInfo on success; Error on failure.
    [[nodiscard]] virtual Result<StreamInfo> open(const std::filesystem::path& path) = 0;

    /// Decodes up to `destination.frames` frames into the pre-allocated
    /// planar buffer `destination`.  Returns the number of frames actually
    /// decoded (0 means end-of-stream).
    ///
    /// The caller owns the memory pointed to by `destination.planes`.
    ///
    /// \param destination  Pre-allocated planar output buffer.
    /// \return Decoded frame count on success; 0 on EOS; Error on failure.
    [[nodiscard]] virtual Result<std::size_t> read(PlanarFrames destination) = 0;

    /// Seeks to `frame` — the next read() call returns audio starting there.
    /// The seek is relative to the trimmed start of the stream (i.e. after
    /// head_trim has been applied).
    ///
    /// Implementations that do not support seeking should return
    /// ErrorCode::SeekFailed without modifying any state.
    ///
    /// \param frame  Zero-based frame index into the trimmed audio.
    /// \return ok() on success; Error on failure.
    [[nodiscard]] virtual Status seek(std::uint64_t frame) = 0;

    /// Closes the decoder and frees all resources.  The decoder is in a
    /// closed state after this call and may be re-opened.
    ///
    /// Idempotent: calling close() on an already-closed decoder is a no-op.
    virtual void close() noexcept = 0;

    /// Returns the StreamInfo most recently returned by open(), or nullopt if
    /// the decoder is not open.
    [[nodiscard]] virtual std::optional<StreamInfo> stream_info() const noexcept = 0;

    /// Returns true when the decoder is currently open.
    [[nodiscard]] virtual bool is_open() const noexcept = 0;
};

}  // namespace arrow::audio
