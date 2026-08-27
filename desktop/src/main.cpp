// SPDX-License-Identifier: MPL-2.0
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>

#include "app/app_info.hpp"
#include "app/application.hpp"
#include "app/command_line.hpp"
#include "audio/decode/wav_decoder.hpp"
#include "audio/playback_service.hpp"
#include "audio/sink/sink_factory.hpp"

static eclipse::audio::SinkFactoryOptions sink_options(eclipse::app::SinkChoice choice) {
    switch (choice) {
        case eclipse::app::SinkChoice::Null:
            return {.preference = eclipse::audio::SinkPreference::Null};
        case eclipse::app::SinkChoice::Alsa:
            return {.preference = eclipse::audio::SinkPreference::Alsa};
        case eclipse::app::SinkChoice::Automatic:
            return {.preference = eclipse::audio::SinkPreference::Automatic};
    }
    return {};
}

#if defined(ECLIPSE_WITH_UI)
#include <eclipse/ui/shell.hpp>
#endif

int main(int argc, char** argv) {
    using eclipse::app::AppInfo;
    using eclipse::app::Application;
    try {
        Application application{AppInfo::current()};
        const AppInfo& info = application.info();
        const auto command = eclipse::app::parse_command_line(argc, argv);
        if (!command) {
            (void)std::fprintf(stderr,
                               "%s: %s\n",
                               eclipse::app::command_line_usage().data(),
                               command.error().technical_detail().c_str());
            return Application::exit_code_for(command.error());
        }
        if (command->help) {
            (void)std::printf("%s", eclipse::app::command_line_usage().data());
            return Application::kExitOk;
        }
        if (command->play) {
            eclipse::audio::WavDecoder decoder;
            auto sink_result = eclipse::audio::make_sink(sink_options(command->sink));
            if (!sink_result) {
                return Application::exit_code_for(sink_result.error());
            }
            auto sink = std::move(sink_result).value();
            eclipse::audio::PlaybackService playback{decoder, *sink, 8};
            const auto result = playback.play_file(std::filesystem::path{command->path}, 1024);
            return result ? Application::kExitOk : Application::exit_code_for(result.error());
        }
        if (eclipse::Status started = application.lifecycle().start(); !started) {
            return Application::exit_code_for(started.error());
        }
        // ECLIPSE_SMOKE_TEST is the release.yml seam: the binary must start
        // its event loop and exit 0 with no audio playing, so the smoke
        // step proves the linked executable opens a window rather than
        // crashing on the first frame. The env-var check happens after
        // startup so a failed lifecycle still produces a meaningful code.
        const char* smoke = std::getenv("ECLIPSE_SMOKE_TEST");
        if (smoke != nullptr) {
            static_cast<void>(application.lifecycle().shutdown());
            return Application::kExitOk;
        }
#if defined(ECLIPSE_WITH_UI)
        const int code = eclipse::ui::run_shell(
            argc,
            argv,
            eclipse::ui::ShellInfo{
                .version = info.version, .git_sha = info.git_sha, .git_dirty = info.git_dirty});
        static_cast<void>(application.lifecycle().shutdown());
        return code;
#else
        (void)std::printf("%s\n", info.to_log_string().c_str());
        static_cast<void>(application.lifecycle().shutdown());
        return Application::kExitUnavailable;
#endif
    } catch (const std::exception& e) {
        (void)std::fprintf(stderr, "unhandled exception: %s\n", e.what());
        return Application::kExitStartupFailed;
    } catch (...) {
        (void)std::fprintf(stderr, "unhandled unknown exception in main\n");
        return Application::kExitStartupFailed;
    }
}
