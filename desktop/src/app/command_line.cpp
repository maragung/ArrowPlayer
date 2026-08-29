// SPDX-License-Identifier: MPL-2.0
//
// Command-line parser and single-instance IPC implementation.

#include "app/command_line.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace arrow::app {

namespace {

// IPC server state.
static std::atomic<bool> g_server_running{false};
#if defined(_WIN32)
static HANDLE g_named_pipe{INVALID_HANDLE_VALUE};
#else
static int g_unix_socket{-1};
#endif

constexpr char IPC_PIPE_NAME[] = R"(\\.\pipe\arrow-player-ipc)";
constexpr char IPC_SOCKET_PATH[] = "/tmp/arrow-player-ipc.sock";

}  // namespace

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

Result<CommandLine> parse_command_line(const int argc, char* const argv[]) noexcept {
    if (argc < 1 || argv == nullptr) {
        return err(ErrorCode::InvalidArgument, "Invalid command line.", "argc/argv is null");
    }

    CommandLine result;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view{argv[i]} : std::string_view{};

        if (arg == "--help" || arg == "-h") {
            result.help = true;
        } else if (arg == "--version" || arg == "-v") {
            result.version = true;
        } else if (arg == "--play") {
            if (i + 1 >= argc || !argv[i + 1] || !argv[i + 1][0]) {
                return err(ErrorCode::InvalidArgument,
                           "A file path is required after --play.",
                           "--play");
            }
            result.play = true;
            result.path = argv[++i];
        } else if (arg == "--enqueue") {
            if (i + 1 >= argc || !argv[i + 1] || !argv[i + 1][0]) {
                return err(ErrorCode::InvalidArgument,
                           "A file path is required after --enqueue.",
                           "--enqueue");
            }
            result.enqueue = true;
            result.path = argv[++i];
        } else if (arg == "--playlist") {
            if (i + 1 >= argc || !argv[i + 1] || !argv[i + 1][0]) {
                return err(ErrorCode::InvalidArgument,
                           "A playlist name is required after --playlist.",
                           "--playlist");
            }
            result.playlist = argv[++i];
        } else if (arg == "--quit") {
            result.quit = true;
        } else if (arg == "--hide") {
            result.hide = true;
        } else if (arg == "--sink") {
            if (i + 1 >= argc || !argv[i + 1]) {
                return err(ErrorCode::InvalidArgument,
                           "--sink requires a value: auto, null, alsa, wasapi, or pulse.",
                           "--sink");
            }
            const std::string_view value{argv[++i]};
            if (value == "auto") {
                result.sink = SinkChoice::Automatic;
            } else if (value == "null") {
                result.sink = SinkChoice::Null;
            } else if (value == "alsa") {
                result.sink = SinkChoice::Alsa;
            } else if (value == "wasapi") {
                result.sink = SinkChoice::Wasapi;
            } else if (value == "pulse") {
                result.sink = SinkChoice::Pulse;
            } else {
                return err(ErrorCode::InvalidArgument,
                           "Unknown sink choice '" + std::string{value} + "'.",
                           std::string{"--sink "} + std::string{value});
            }
        } else {
            return err(ErrorCode::InvalidArgument,
                       "Unknown command-line option: '" + std::string{arg} + "'.",
                       std::string{arg});
        }
    }

    // Mutual exclusivity constraints.
    if (result.help && (result.play || result.enqueue || result.quit || result.hide)) {
        return err(ErrorCode::InvalidArgument,
                   "--help cannot be combined with playback or control actions.",
                   "--help with playback");
    }
    if (result.version && (result.play || result.enqueue || result.help ||
                           result.quit || result.hide || !result.playlist.empty())) {
        return err(ErrorCode::InvalidArgument,
                   "--version cannot be combined with other options.",
                   "--version with other options");
    }
    if ((result.play || result.enqueue) && (result.quit || result.hide)) {
        return err(ErrorCode::InvalidArgument,
                   "Playback options cannot be combined with --quit or --hide.",
                   "playback + quit/hide");
    }
    if (!result.play && !result.enqueue && !result.playlist.empty() &&
        result.sink != SinkChoice::Automatic) {
        return err(ErrorCode::InvalidArgument,
                   "--sink requires --play, --enqueue, or --playlist.",
                   "--sink without playback");
    }

    return result;
}

std::string_view command_line_usage() noexcept {
    return "Usage: arrow-player [options]\n"
           "Playback:\n"
           "  --play FILE         play a single audio file and exit\n"
           "  --enqueue FILE      add a file to the current playback queue\n"
           "  --playlist NAME     load a named playlist\n"
           "Control:\n"
           "  --quit              ask the running instance to quit\n"
           "  --hide              ask the running instance to hide the window\n"
           "Output sink (with --play / --enqueue / --playlist):\n"
           "  --sink auto         automatically choose the best available sink (default)\n"
           "  --sink null         null / no-output sink (for benchmarking)\n"
           "  --sink alsa         ALSA (Linux)\n"
           "  --sink wasapi       WASAPI (Windows)\n"
           "  --sink pulse        PulseAudio (Linux)\n"
           "Info:\n"
           "  --help, -h          show this help and exit\n"
           "  --version, -v       print the version and exit\n";
}

// ---------------------------------------------------------------------------
// Serialisation helpers
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::vector<char> serialise_message(const IpcMessage& msg) noexcept {
    std::vector<char> out;
    out.push_back(static_cast<char>(msg.tag));
    std::uint32_t len = static_cast<std::uint32_t>(msg.payload.size());
    out.insert(out.end(),
               reinterpret_cast<const char*>(&len),
               reinterpret_cast<const char*>(&len) + sizeof(len));
    out.insert(out.end(), msg.payload.begin(), msg.payload.end());
    return out;
}

[[nodiscard]] IpcMessage deserialise_message(const char* data, const std::size_t size) noexcept {
    IpcMessage msg;
    if (size < 1) return msg;
    msg.tag = static_cast<IpcMessage::Tag>(static_cast<unsigned char>(data[0]));
    if (size >= 1 + sizeof(std::uint32_t)) {
        std::uint32_t len = 0;
        std::memcpy(&len, data + 1, sizeof(len));
        if (len > 0 && size >= 1 + sizeof(len) + len) {
            msg.payload.assign(data + 1 + sizeof(len),
                               data + 1 + sizeof(len) + len);
        }
    }
    return msg;
}

}  // namespace

// ---------------------------------------------------------------------------
// Single-instance IPC — Windows named pipe
// ---------------------------------------------------------------------------

#if defined(_WIN32)

[[nodiscard]] bool try_send_to_running_instance(const IpcMessage& message) noexcept {
    const auto data = serialise_message(message);

    const HANDLE pipe = CreateFileA(
        IPC_PIPE_NAME,
        GENERIC_WRITE,
        0,  // no sharing
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE) {
        // No existing instance is running — we are the primary.
        return false;
    }

    DWORD written = 0;
    WriteFile(pipe, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(pipe);
    return true;  // Message sent; caller should exit.
}

void run_ipc_server(std::function<void(IpcMessage)> on_message,
                   std::function<void()> on_disconnect) noexcept {
    g_server_running.store(true, std::memory_order_release);

    while (g_server_running.load(std::memory_order_acquire)) {
        const HANDLE pipe = CreateNamedPipeA(
            IPC_PIPE_NAME,
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
            1,   // max instances
            4096,  // out buffer
            4096,  // in buffer
            0,   // default timeout
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) break;

        // Wait for a client to connect.
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const BOOL connected = ConnectNamedPipe(pipe, &ov);
        const DWORD err = GetLastError();

        if (!connected && err != ERROR_IO_PENDING && err != ERROR_PIPE_CONNECTED) {
            CloseHandle(pipe);
            if (ov.hEvent) CloseHandle(ov.hEvent);
            break;
        }

        // Wait for the connection.
        if (err == ERROR_IO_PENDING) {
            WaitForSingleObject(ov.hEvent, INFINITE);
        }
        if (ov.hEvent) CloseHandle(ov.hEvent);

        // Read the message.
        char buffer[4096];
        DWORD read = 0;
        if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            const auto msg = deserialise_message(buffer, static_cast<std::size_t>(read));
            if (on_message) on_message(msg);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);

        if (on_disconnect) on_disconnect();
    }
}

void stop_ipc_server() noexcept {
    g_server_running.store(false, std::memory_order_release);
    // On Windows, the next CreateNamedPipe call will fail gracefully.
}

#else  // Unix domain socket

// ---------------------------------------------------------------------------
// Single-instance IPC — Unix domain socket
// ---------------------------------------------------------------------------

[[nodiscard]] bool try_send_to_running_instance(const IpcMessage& message) noexcept {
    const auto data = serialise_message(message);

    const int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    const int ret = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
        close(sock);
        return false;  // No server running.
    }

    std::uint32_t len = static_cast<std::uint32_t>(data.size());
    write(sock, &len, sizeof(len));
    write(sock, data.data(), data.size());
    close(sock);
    return true;  // Message sent; caller should exit.
}

void run_ipc_server(std::function<void(IpcMessage)> on_message,
                   std::function<void()> on_disconnect) noexcept {
    g_server_running.store(true, std::memory_order_release);

    // Remove any stale socket file.
    unlink(IPC_SOCKET_PATH);

    g_unix_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_unix_socket < 0) return;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(g_unix_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(g_unix_socket);
        g_unix_socket = -1;
        return;
    }

    if (listen(g_unix_socket, 4) < 0) {
        close(g_unix_socket);
        g_unix_socket = -1;
        unlink(IPC_SOCKET_PATH);
        return;
    }

    while (g_server_running.load(std::memory_order_acquire)) {
        int client = accept(g_unix_socket, nullptr, nullptr);
        if (client < 0) {
            if (g_server_running.load(std::memory_order_acquire)) continue;
            break;
        }

        std::uint32_t msg_len = 0;
        if (read(client, &msg_len, sizeof(msg_len)) == sizeof(msg_len)) {
            std::vector<char> buf(static_cast<std::size_t>(msg_len));
            std::size_t total = 0;
            while (total < buf.size()) {
                const auto n = read(client, buf.data() + total, buf.size() - total);
                if (n <= 0) break;
                total += static_cast<std::size_t>(n);
            }
            if (total == buf.size()) {
                const auto msg = deserialise_message(buf.data(), buf.size());
                if (on_message) on_message(msg);
            }
        }

        close(client);
        if (on_disconnect) on_disconnect();
    }

    close(g_unix_socket);
    g_unix_socket = -1;
    unlink(IPC_SOCKET_PATH);
}

void stop_ipc_server() noexcept {
    g_server_running.store(false, std::memory_order_release);
    if (g_unix_socket >= 0) {
        shutdown(g_unix_socket, SHUT_RDWR);
    }
}

#endif  // platform

}  // namespace arrow::app
