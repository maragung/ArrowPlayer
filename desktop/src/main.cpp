// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// Entry point — §28 Phase 0 ("CMake + presets producing a Qt hello window",
// exit gate 1 "window opens", exit gate 7 "version shown in About").
//
// This file is the composition root: the one translation unit that may see both
// layer 4 (app/) and layer 5 (ui/), because somebody has to hand one to the
// other and §7.2 rule 1 forbids layer 4 from naming layer 5.
//
// §5's layout describes this file as "single-instance check, CLI parse,
// bootstrap". Only the bootstrap is here. §28 Phase 4 owns "single-instance
// IPC, CLI, safe mode, portable mode", and a stub argument parser that accepted
// flags it did not honour would be worse than their absence — it would look
// like the feature exists. So argc/argv are passed straight to the shell, which
// is what Qt needs for its own platform arguments, and nothing else reads them
// until Phase 4. Recorded as OQ-054 in docs/OPEN-QUESTIONS.md.

#include <cstdio>
#include <exception>
#include <string_view>

#include "app/app_info.hpp"
#include "app/application.hpp"

#if defined(ECLIPSE_WITH_UI)
#include <eclipse/ui/shell.hpp>
#endif

int main(int argc, char** argv) {
    using eclipse::app::AppInfo;
    using eclipse::app::Application;

    // bugprone-exception-escape (REQ-SEC-015): main must not let an exception
    // escape. AppInfo/lifecycle/shell can throw (allocations), so the whole body
    // is wrapped; a caught exception names itself on stderr and exits EX_SOFTWARE
    // rather than terminating with a bare std::terminate.
    try {
        Application application{AppInfo::current()};
        const AppInfo& info = application.info();

        // Phase 0 registers no steps; see app/lifecycle.hpp for why an empty
        // lifecycle is still started and stopped rather than skipped.
        if (eclipse::Status started = application.lifecycle().start(); !started) {
            const eclipse::Error& error = started.error();
            // to_string returns a string_view over a literal; print it with an
            // explicit length rather than assuming null termination.
            const std::string_view code_name = eclipse::to_string(error.code());
            (void)std::fprintf(stderr,
                               "%s\n%.*s: %s\n",
                               info.to_log_string().c_str(),
                               static_cast<int>(code_name.size()),
                               code_name.data(),
                               error.technical_detail().c_str());
            return Application::exit_code_for(error);
        }

#if defined(ECLIPSE_WITH_UI)
        const int code = eclipse::ui::run_shell(argc,
                                                argv,
                                                eclipse::ui::ShellInfo{
                                                    .version = info.version,
                                                    .git_sha = info.git_sha,
                                                    .git_dirty = info.git_dirty,
                                                });
        static_cast<void>(application.lifecycle().shutdown());
        return code;
#else
        // No window, and the exit code says so. Exit gate 1 is "window opens"; a
        // build with no UI cannot satisfy it, and returning 0 here is precisely how
        // a green smoke test would come to mean nothing (the OQ-042 shape recorded
        // in docs/OPEN-QUESTIONS.md). EX_UNAVAILABLE is the honest answer: the
        // program is intact, this build simply cannot do its job.
        static_cast<void>(argc);
        static_cast<void>(argv);
        (void)std::printf("%s\n", info.to_log_string().c_str());
        (void)std::fprintf(stderr,
                           "This build contains no user interface, so no window can open.\n"
                           "Reconfigure with -DECLIPSE_BUILD_UI=ON and a Qt 6.8+ installation\n"
                           "that CMake can find (see docs/BUILDING.md).\n");
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
