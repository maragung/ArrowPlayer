// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <string_view>

#include "core/error.hpp"

namespace eclipse::app {

enum class SinkChoice {
    Automatic,
    Null,
    Alsa,
};

struct CommandLine {
    bool help{false};
    bool play{false};
    std::string path;
    SinkChoice sink{SinkChoice::Automatic};
};

[[nodiscard]] Result<CommandLine> parse_command_line(int argc, char* const argv[]) noexcept;
[[nodiscard]] std::string_view command_line_usage() noexcept;

}  // namespace eclipse::app
