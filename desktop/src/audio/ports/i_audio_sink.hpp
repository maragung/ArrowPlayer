// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Arrow Player contributors
#pragma once

// Must be first — ragel/config.hpp guards platform-specific symbols used below.
#include <ragel/config.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/ports/audio_types.hpp"  // for SinkConfig and PcmFormat
#include "core/error.hpp"

namespace arrow::audio {

// Forward declaration — used by DeviceLostCallback below.
class IAudioSink;

// ===========================================================================
//  Device enumeration
// ===========================================================================

/// Identifies a physical audio device on the system.
using DeviceId = std::uint32_t;

/// Invalid device sentinel.
inline constexpr DeviceId kInvalidDeviceId = ~DeviceId{0};

/// Description of an enumerated audio device.
struct DeviceInfo final {
    DeviceId id = kInvalidDeviceId;
    /// Human-readable name for display in the device selector.
    std::string name;
    /// True when this is the system default device.
    bool is_default = false;
    /// Maximum number of output channels this device supports.
    std::uint16_t max_channels = 0;
    /// Supported sample rates (empty = query failed).
    std::vector<std::uint32_t> sample_rates;
    /// True when the device is currently open by another application in exclusive
    /// mode.
    bool is_exclusive = false;
    /// True when the device supports bit-exact / exclusive-mode output.
    bool supports_exclusive = false;
};

/// Result of format negotiation between the engine and the device.
struct NegotiatedFormat final {
    /// The format the device accepted.
    PcmFormat format{};
    /// True when the device accepted the requested format without resampling.
    bool bit_exact = false;
    /// Estimated output latency in frames (device + buffer).
    std::size_t latency_frames = 0;
    /// Device-reported period size in frames (for polling-based APIs).
    std::size_t period_frames = 0;
};

// ===========================================================================
//  Operation mode
// ===========================================================================

/// Operating mode for an audio sink.
enum class SinkMode : std::uint8_t {
    /// Shared mode (also called "polling" or "mmap" on some backends). The OS
    /// mixes with other applications. Lowest latency without exclusive access.
    Shared = 0,
    /// Exclusive / "raw" mode. The device is reserved for this application.
    /// Required for bit-perfect output (REQ-AUD-113). May fail if another app
    /// already holds exclusive access.
    Exclusive = 1,
};

// ===========================================================================
//  Device-lost callback
// ===========================================================================

/// Callback invoked when the underlying device is unplugged, disconnected, or
/// taken by another process. The engine will have already called close() by the
/// time this fires.
///
/// `sink` is the IAudioSink that fired the event. The caller can use it to
/// open a new device or surface a notification to the user.
///
/// The callback is called from the audio backend's internal thread — it is NOT
/// the RT audio thread, so it may block and allocate freely.
using DeviceLostCallback = std::function<void(IAudioSink& sink)>;

// ===========================================================================
//  Audio callback
// ===========================================================================

/// Pull-based audio callback: the sink calls this function when it needs more
/// audio data. The callee fills `destination` with up to `destination.frames`
/// frames of float32 planar audio.
///
/// The callback is invoked from the audio backend's thread — NOT the RT audio
/// thread in the sense of "may call blocking operations" — but the audio
/// backend will itself be called from the RT audio thread of whatever audio
/// server it uses (PulseAudio, WASAPI, CoreAudio, etc.). Therefore the callee
/// must still be RT-safe in the sense of no locks, no syscalls, no heap
/// allocation.
///
/// `userdata` is the opaque pointer registered via set_callback().
using AudioCallback = std::function<void(PlanarFrames destination, void* userdata)>;

// ===========================================================================
//  IAudioSink — abstract audio output port
// ===========================================================================

/// Abstract interface for all audio output devices.
///
/// Implementors own the audio backend (ALSA, WASAPI, CoreAudio, PulseAudio,
/// etc.). The engine pushes samples by calling write(); the sink pulls from the
/// registered callback to obtain those samples.
///
/// All public methods are NOT RT-safe unless otherwise noted: they may block,
/// allocate, and hold locks.
///
/// RT-SAFE note: the registered AudioCallback is called from the audio backend's
/// internal thread. The callback's callee must be RT-safe.
class IAudioSink {
  public:
    virtual ~IAudioSink() = default;

    // -------------------------------------------------------------------------
    //  Device enumeration
    // -------------------------------------------------------------------------

    /// Returns a list of all available output devices.  The list is static for
    /// the lifetime of the process — a device appearing or disappearing after
    /// the first call requires a process restart.
    ///
    /// \return Vector of DeviceInfo, sorted with the default device first.
    [[nodiscard]] virtual std::vector<DeviceInfo> enumerate() = 0;

    /// Returns the device ID for the system default output device.
    [[nodiscard]] virtual DeviceId default_device() const noexcept = 0;

    // -------------------------------------------------------------------------
    //  Lifecycle
    // -------------------------------------------------------------------------

    /// Opens `device` with `format` and `mode`.  After a successful open(), the
    /// device is ready to receive write() calls.  If the device cannot provide
    /// `format` natively, the implementation may open it with a different format
    /// and perform on-device resampling; the caller can inspect the returned
    /// NegotiatedFormat to determine whether bit-perfect output is possible.
    ///
    /// \param device   Device to open (use default_device() for the system default).
    /// \param format   Requested output format.
    /// \param mode     Shared or Exclusive.
    /// \return Negotiated format on success; Error on failure.
    [[nodiscard]] virtual Result<NegotiatedFormat> open(
        DeviceId device,
        const PcmFormat& format,
        SinkMode mode) = 0;

    /// Starts audio output.  After start(), the sink will call the registered
    /// AudioCallback (see set_callback) whenever it needs audio data.
    ///
    /// Requires a prior open().  Calling start() on an already-started device
    /// is a no-op that returns ok().
    [[nodiscard]] virtual Status start() = 0;

    /// Stops audio output immediately.  The callback will not be called again
    /// until the next start().
    ///
    /// Idempotent: calling stop() on a stopped device is a no-op.
    virtual void stop() noexcept = 0;

    /// Closes the device and releases all resources.  The sink enters a closed
    /// state and may be re-opened.
    ///
    /// Idempotent: calling close() on an already-closed sink is a no-op.
    virtual void close() noexcept = 0;

    // -------------------------------------------------------------------------
    //  Playback control
    // -------------------------------------------------------------------------

    /// Pushes `frames` of audio into the device's internal buffer.
    /// This is the pull-based equivalent of "write samples" — the samples are
    /// queued and handed to the backend on its next pull cycle.
    ///
    /// RT-SAFE: the implementation must be RT-safe.  If the internal buffer is
    /// full, the call may block briefly (bounded by the device period).
    ///
    /// \param frames  Planar float audio to enqueue.
    /// \return ok() on success; Error on failure.
    [[nodiscard]] virtual Status write(PlanarFrames frames) noexcept = 0;

    /// Returns the current latency estimate in frames (device + buffer).
    /// Returns 0 if the device is not open.
    [[nodiscard]] virtual std::size_t latency_frames() const noexcept = 0;

    // -------------------------------------------------------------------------
    //  Callbacks
    // -------------------------------------------------------------------------

    /// Registers a callback that the sink invokes whenever it needs audio.
    /// The callback is owned by the caller; it must remain valid for the
    /// lifetime of the sink or until replaced by another call to set_callback.
    ///
    /// Setting a null callback is valid and causes the sink to output silence.
    virtual void set_callback(AudioCallback callback, void* userdata) noexcept = 0;

    /// Registers a DeviceLostCallback.  The sink stores a copy of `fn` and
    /// invokes it when the underlying device is lost.  Calling with a default-
    /// constructed callback (nullptr) unregisters the previous handler.
    virtual void on_device_lost(DeviceLostCallback fn) noexcept = 0;

    // -------------------------------------------------------------------------
    //  State queries
    // -------------------------------------------------------------------------

    /// True when open() has succeeded and close() has not been called.
    [[nodiscard]] virtual bool is_open() const noexcept = 0;

    /// True when start() has succeeded and stop() has not been called.
    [[nodiscard]] virtual bool is_started() const noexcept = 0;

    /// Returns the negotiated format from the last successful open(), or nullopt
    /// if the sink is not open.
    [[nodiscard]] virtual std::optional<NegotiatedFormat> negotiated_format() const noexcept = 0;

    /// Returns the device name string reported by the backend, or "unknown"
    /// when the sink is not open.
    [[nodiscard]] virtual std::string_view device_name() const noexcept = 0;
};

}  // namespace arrow::audio
