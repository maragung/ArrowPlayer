// SPDX-License-Identifier: MPL-2.0
// Copyright (c) Eclipse Player contributors
//
// Startup and shutdown ordering — layer 4 (APPLICATION) per §7.1.
//
// Every subsystem later phases add — the audio engine (§8.2), the library
// scanner (§9.1), the plugin host (§16.3) — has a start that can fail and a
// teardown that must happen in the opposite order. Doing that by hand in
// main.cpp works exactly until the third subsystem, at which point a failed
// start leaves the two before it running and nothing says so.
//
// Phase 0 registers no steps. That is the honest state of the program, not a
// placeholder: an empty lifecycle still exercises start/shutdown and still
// rejects every illegal transition, so the ordering guarantee is under test
// before there is anything whose order could be got wrong.
//
// Not real-time safe. Steps are std::function, so registering and starting
// allocate. Nothing here may be called from the audio callback (REQ-AUD-015).

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.hpp"

namespace eclipse::app {

/// Where a Lifecycle is in its one-way journey.
///
/// `Failed` and `Stopped` are terminal. A Lifecycle is not a resettable object:
/// restarting a process's subsystems in place is how half-initialised state
/// survives a failure, so this type refuses rather than offering the option.
enum class LifecycleState {
    Created,       ///< accepting steps; nothing started
    Starting,      ///< inside start(); visible only to a step's own callback
    Running,       ///< every step started
    ShuttingDown,  ///< inside shutdown()
    Stopped,       ///< shut down cleanly (terminal)
    Failed         ///< a step failed to start; started steps were unwound (terminal)
};

[[nodiscard]] std::string_view to_string(LifecycleState state) noexcept;

/// An ordered list of named startup steps, torn down in reverse.
///
/// Not thread-safe: one owner, normally the thread that runs main(). Calling
/// start() and shutdown() from different threads without external
/// synchronisation is a data race.
class Lifecycle {
  public:
    /// Bring a subsystem up. Returning an error aborts startup.
    using StartFn = std::function<Status()>;

    /// Tear a subsystem down. Returns nothing on purpose: during shutdown there
    /// is no caller left to hand a failure to and no meaningful recovery, so a
    /// step that can fail must report through the log facade (§22.2) and finish
    /// the teardown regardless. REQ-GEN-063 forbids silent catch-and-ignore, and
    /// a Status nobody could act on would be exactly that with extra ceremony.
    using StopFn = std::function<void()>;

    Lifecycle() = default;

    /// Appends a step. Steps run in registration order and stop in reverse.
    ///
    /// Errors: `InvalidArgument` for an empty name or an empty start callback;
    /// `InvalidState` once start() has been called.
    Status add_step(std::string name, StartFn start, StopFn stop = {});

    /// Starts every step in order.
    ///
    /// On failure the steps that *did* start are stopped in reverse order, the
    /// state becomes `Failed`, and the returned error carries the failing step's
    /// name in its technical detail. The failing step's own stop callback is not
    /// called: it never reported a successful start, so it has nothing to undo,
    /// and calling it would mean tearing down state that may not exist.
    ///
    /// Errors: `InvalidState` unless the state is `Created`; otherwise whatever
    /// the failing step returned.
    Status start();

    /// Stops every started step in reverse order. Errors: `InvalidState` unless
    /// the state is `Running`.
    Status shutdown();

    [[nodiscard]] LifecycleState state() const noexcept { return state_; }

    [[nodiscard]] std::size_t step_count() const noexcept { return steps_.size(); }

    /// Names of the steps currently started, in start order. Cleared by a
    /// successful shutdown and by the unwind of a failed start, so it is also
    /// the assertion a test uses to prove nothing was left running.
    [[nodiscard]] const std::vector<std::string>& started() const noexcept { return started_; }

  private:
    struct Step {
        std::string name;
        StartFn start;
        StopFn stop;
    };

    void unwind() noexcept;

    std::vector<Step> steps_;
    std::vector<std::string> started_;
    LifecycleState state_{LifecycleState::Created};
};

}  // namespace eclipse::app
