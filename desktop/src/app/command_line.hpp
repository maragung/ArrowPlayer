// SPDX-License-Identifier: MPL-2.0
//
// Command-line argument parser and single-instance IPC.
// Per SPEC §7.1.3 — supports: --play, --enqueue, --playlist, --quit, --hide.
// Single-instance: if already running, sends an IPC message to the existing instance
// and exits.  IPC uses a platform-specific named pipe (Windows) or Unix domain socket
// (Linux/macOS).

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.hpp"

namespace arrow::app {

/// Output sink choice.
enum class SinkChoice {
    Automatic,
    Null,
    Alsa,
    Wasapi,
    Pulse,
};

/// Parsed command-line arguments.
struct CommandLine final {
    bool help{false};
    bool version{false};
    bool play{false};         ///< --play: play a single file and exit
    bool enqueue{false};     ///< --enqueue: add to the current queue
    bool quit{false};        ///< --quit: ask the running instance to quit
    bool hide{false};        ///< --hide: ask the running instance to hide
    std::string path;        ///< path for --play or --enqueue
    std::string playlist;    ///< playlist name for --playlist
    SinkChoice sink{SinkChoice::Automatic};
};

/// IPC message sent to the existing instance (single-instance mode).
struct IpcMessage final {
    enum class Tag : std::uint8_t {
        PlayFile,
        EnqueueFile,
        LoadPlaylist,
        Show,
        Hide,
        Quit,
    };

    Tag tag{Tag::Quit};
    std::string payload;  // file path or playlist name
};

/// Parses the standard C argv array.  Returns an error if the arguments are
/// malformed or incompatible.
[[nodiscard]] Result<CommandLine> parse_command_line(int argc, char* const argv[]) noexcept;

/// Returns the single-line usage text for --help output.
[[nodiscard]] std::string_view command_line_usage() noexcept;

// ---------------------------------------------------------------------------
// Single-instance IPC
// ---------------------------------------------------------------------------

/// Tries to connect to an existing Arrow Player instance.
/// If a connection is made, sends the message and returns true (caller should exit).
/// If no instance is running, creates the server socket and returns false.
[[nodiscard]] bool try_send_to_running_instance(const IpcMessage& message) noexcept;

/// Starts the IPC server loop on the current thread.  Calls on_message for each
/// received IpcMessage.  Blocks until stop_server() is called or the connection
/// is lost.
void run_ipc_server(
    std::function<void(IpcMessage)> on_message,
    std::function<void()> on_disconnect = nullptr) noexcept;

/// Signals the IPC server to stop.
void stop_ipc_server() noexcept;

}  // namespace arrow::app
