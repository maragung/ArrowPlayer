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

static arrow::audio::SinkFactoryOptions sink_options(arrow::app::SinkChoice choice) {
    switch (choice) {
        case arrow::app::SinkChoice::Null:
            return {.preference = arrow::audio::SinkPreference::Null};
        case arrow::app::SinkChoice::Alsa:
            return {.preference = arrow::audio::SinkPreference::Alsa};
        case arrow::app::SinkChoice::Automatic:
            return {.preference = arrow::audio::SinkPreference::Automatic};
    }
    return {};
}

#if defined(ARROW_WITH_UI)
#include <arrow/ui/shell.hpp>
#endif

int main(int argc, char** argv) {
    using arrow::app::AppInfo;
    using arrow::app::Application;
    try {
        Application application{AppInfo::current()};
        const AppInfo& info = application.info();
        const auto command = arrow::app::parse_command_line(argc, argv);
        if (!command) {
            (void)std::fprintf(stderr,
                               "%s: %s\n",
                               arrow::app::command_line_usage().data(),
                               command.error().technical_detail().c_str());
            return Application::exit_code_for(command.error());
        }
        if (command->help) {
            (void)std::printf("%s", arrow::app::command_line_usage().data());
            return Application::kExitOk;
        }
        if (command->version) {
            // Mirrors the Qt About dialog (MainWindow::aboutText) so a `--version`
            // invocation and Help → About cannot report different build
            // identities. Exits 0 before the lifecycle is started: a request for
            // version information must not require the audio stack.
            (void)std::printf("%s", info.to_about_text().c_str());
            return Application::kExitOk;
        }
        if (command->play) {
            arrow::audio::WavDecoder decoder;
            auto sink_result = arrow::audio::make_sink(sink_options(command->sink));
            if (!sink_result) {
                return Application::exit_code_for(sink_result.error());
            }
            auto sink = std::move(sink_result).value();
            arrow::audio::PlaybackService playback{decoder, *sink, 8};
            const auto result = playback.play_file(std::filesystem::path{command->path}, 1024);
            return result ? Application::kExitOk : Application::exit_code_for(result.error());
        }
        if (arrow::Status started = application.lifecycle().start(); !started) {
            return Application::exit_code_for(started.error());
        }
        // ARROW_SMOKE_TEST is the release.yml seam: the binary must start
        // its event loop and exit 0 with no audio playing, so the smoke
        // step proves the linked executable opens a window rather than
        // crashing on the first frame. The env-var check happens after
        // startup so a failed lifecycle still produces a meaningful code.
        // getenv is fine for a single read of a build-time-only variable;
        // MSVC's C4996 deprecation is the documented case, so the local
        // pragma is the narrowest suppression.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv is deprecated in favour of _dupenv_s
#endif
        const char* smoke = std::getenv("ARROW_SMOKE_TEST");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (smoke != nullptr) {
            static_cast<void>(application.lifecycle().shutdown());
            return Application::kExitOk;
        }
#if defined(ARROW_WITH_UI)
        const int code = arrow::ui::run_shell(
            argc,
            argv,
            arrow::ui::ShellInfo{
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
