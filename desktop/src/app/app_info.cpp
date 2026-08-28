// SPDX-License-Identifier: MPL-2.0
#include "app/app_info.hpp"

#include <arrow/version.hpp>

namespace arrow::app {

AppInfo AppInfo::current() noexcept {
    return AppInfo{
        .name = version::kName,
        .version = version::kString,
        .git_sha = version::kGitSha,
        .git_dirty = version::kGitDirty != 0,
        .major = version::kMajor,
        .minor = version::kMinor,
        .patch = version::kPatch,
    };
}

std::string AppInfo::to_log_string() const {
    std::string out;
    out.reserve(name.size() + version.size() + git_sha.size() + 40);
    out.append(name).append(" ").append(version);
    out.append(" (").append(git_sha);
    // Spelled out rather than a "+dirty" suffix: this string is pasted into bug
    // reports by people who did not build it, and a sigil they have to look up
    // is a sigil they will omit.
    if (git_dirty) {
        out.append(", working tree modified");
    }
    out.append(")");
    return out;
}

}  // namespace arrow::app
