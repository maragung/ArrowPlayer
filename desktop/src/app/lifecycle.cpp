// SPDX-License-Identifier: MPL-2.0
#include "app/lifecycle.hpp"

#include <utility>

namespace eclipse::app {

std::string_view to_string(LifecycleState state) noexcept {
    switch (state) {
        case LifecycleState::Created:      return "created";
        case LifecycleState::Starting:     return "starting";
        case LifecycleState::Running:      return "running";
        case LifecycleState::ShuttingDown: return "shutting-down";
        case LifecycleState::Stopped:      return "stopped";
        case LifecycleState::Failed:       return "failed";
    }
    return "unknown";
}

Status Lifecycle::add_step(std::string name, StartFn start, StopFn stop) {
    if (name.empty()) {
        return err(ErrorCode::InvalidArgument,
                   "A startup step was registered without a name.",
                   "Lifecycle::add_step called with an empty name; the name is what a "
                   "failed start reports, so an unnamed step cannot be diagnosed.");
    }
    if (!start) {
        return err(ErrorCode::InvalidArgument,
                   "A startup step was registered without anything to run.",
                   "Lifecycle::add_step called with an empty StartFn for step '" + name + "'.");
    }
    if (state_ != LifecycleState::Created) {
        return err(ErrorCode::InvalidState,
                   "A startup step was added after startup had already begun.",
                   "Lifecycle::add_step for step '" + name + "' while state is " +
                       std::string{to_string(state_)} + "; steps are fixed once start() runs, "
                       "because a step appended later would never be stopped.");
    }
    steps_.push_back(Step{std::move(name), std::move(start), std::move(stop)});
    return ok();
}

Status Lifecycle::start() {
    if (state_ != LifecycleState::Created) {
        return err(ErrorCode::InvalidState,
                   "The application was asked to start twice.",
                   "Lifecycle::start while state is " + std::string{to_string(state_)} +
                       "; Created is the only state that may start.");
    }
    state_ = LifecycleState::Starting;
    started_.reserve(steps_.size());

    for (const Step& step : steps_) {
        Status result = step.start();
        if (!result) {
            Error failure = std::move(result).error();
            // Record which step failed before unwinding, so the message survives
            // whatever the teardown of the earlier steps does.
            const std::string detail = "startup step '" + step.name + "' failed: " +
                                       failure.technical_detail();
            unwind();
            state_ = LifecycleState::Failed;
            return failure.with_detail(detail);
        }
        started_.push_back(step.name);
    }

    state_ = LifecycleState::Running;
    return ok();
}

Status Lifecycle::shutdown() {
    if (state_ != LifecycleState::Running) {
        return err(ErrorCode::InvalidState,
                   "The application was asked to shut down before it was running.",
                   "Lifecycle::shutdown while state is " + std::string{to_string(state_)} +
                       "; Running is the only state that may shut down.");
    }
    state_ = LifecycleState::ShuttingDown;
    unwind();
    state_ = LifecycleState::Stopped;
    return ok();
}

void Lifecycle::unwind() noexcept {
    // Reverse order, and only the steps that actually reported a successful
    // start. `started_` is indexed against `steps_` positionally because steps
    // are append-only and fixed once start() begins.
    for (std::size_t i = started_.size(); i > 0; --i) {
        const Step& step = steps_[i - 1];
        if (step.stop) {
            step.stop();
        }
    }
    started_.clear();
}

}  // namespace eclipse::app
