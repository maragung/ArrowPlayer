// SPDX-License-Identifier: MPL-2.0
#include "app/command_line.hpp"

namespace arrow::app {

Result<CommandLine> parse_command_line(int argc, char* const argv[]) noexcept {
    if (argc < 1 || argv == nullptr) {
        return err(ErrorCode::InvalidArgument, "Invalid command line.", "argc/argv");
    }
    CommandLine result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] == nullptr ? std::string_view{} : argv[i];
        if (arg == "--help" || arg == "-h") {
            result.help = true;
        } else if (arg == "--version" || arg == "-v") {
            result.version = true;
        } else if (arg == "--play") {
            if (result.play || i + 1 >= argc || argv[i + 1] == nullptr ||
                std::string_view{argv[i + 1]}.empty()) {
                return err(ErrorCode::InvalidArgument,
                           "A single audio file is required after --play.",
                           "--play");
            }
            result.play = true;
            result.path = argv[++i];
        } else if (arg == "--sink") {
            if (i + 1 >= argc || argv[i + 1] == nullptr) {
                return err(ErrorCode::InvalidArgument,
                           "Choose a sink: auto, null, or alsa.",
                           "--sink");
            }
            const std::string_view value{argv[++i]};
            if (value == "auto") {
                result.sink = SinkChoice::Automatic;
            } else if (value == "null") {
                result.sink = SinkChoice::Null;
            } else if (value == "alsa") {
                result.sink = SinkChoice::Alsa;
            } else {
                return err(ErrorCode::InvalidArgument,
                           "Choose a sink: auto, null, or alsa.",
                           std::string{"--sink "} + std::string{value});
            }
        } else {
            return err(
                ErrorCode::InvalidArgument, "Unknown command-line option.", std::string{arg});
        }
    }
    if (result.help && result.play) {
        return err(ErrorCode::InvalidArgument,
                   "--help cannot be combined with playback.",
                   "--help --play");
    }
    if (result.version && (result.play || result.help)) {
        return err(ErrorCode::InvalidArgument,
                   "--version cannot be combined with other options.",
                   "--version --play");
    }
    if (!result.play && result.sink != SinkChoice::Automatic) {
        return err(
            ErrorCode::InvalidArgument, "--sink requires --play.", "sink without playback");
    }
    return result;
}

std::string_view command_line_usage() noexcept {
    return "Usage: arrow-player [--help] [--version] [--play FILE] [--sink auto|null|alsa]\n"
           "  --help, -h       show this help and exit\n"
           "  --version, -v    print the version and exit\n"
           "  --play FILE      play a single audio file and exit\n"
           "  --sink CHOICE    output sink to use with --play: auto, null, or alsa\n";
}

}  // namespace arrow::app
