// SPDX-License-Identifier: MPL-2.0
#include "app/application.hpp"

namespace arrow::app {

int Application::exit_code_for(const Error& error) noexcept {
    switch (error.code()) {
        case ErrorCode::Ok:
            return kExitOk;
        // A subsystem that could not be brought up, or a device/permission the
        // environment did not provide: the program is fine, the world was not.
        case ErrorCode::DeviceNotFound:
        case ErrorCode::DeviceInUse:
        case ErrorCode::PermissionDenied:
        case ErrorCode::NetworkDisabled:
        case ErrorCode::NetworkUnreachable:
            return kExitUnavailable;
        default:
            return kExitStartupFailed;
    }
}

}  // namespace arrow::app
