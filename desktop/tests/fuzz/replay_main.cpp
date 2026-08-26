// SPDX-License-Identifier: MPL-2.0
/// Corpus replay driver — the half of REQ-SEC-011 that works without libFuzzer.
///
/// REQ-SEC-011 asks for two things: fuzz targets, and a *committed, growing*
/// corpus. Only the first needs libFuzzer. This driver takes the second half and
/// turns it into an ordinary test: it feeds every file in a corpus directory to
/// `LLVMFuzzerTestOneInput` and reports the ones that abort.
///
/// That matters for three reasons:
///
///   * REQ-SEC-012 makes any sanitizer finding a release blocker whose input MUST
///     enter the regression corpus. A corpus nothing replays is not a regression
///     suite, it is an archive.
///   * libFuzzer needs clang. This links with whatever compiler builds the
///     project, so the corpus is checked on every configuration — including the
///     GCC `linux-asan` preset, which supplies the ASan+UBSan of REQ-SEC-012.
///   * The corpus is walked at run time, not globbed at configure time, so
///     dropping a crash input into `corpus/<target>/` makes it a regression case
///     immediately. A step that needs a CMake re-run to notice a new seed is a
///     step somebody forgets.
///
/// Exit status is 0 when every input was survived. A crashing input does not
/// produce a non-zero exit: the harness calls `abort()`, so the test fails the way
/// a sanitizer failure fails, with the diagnostic the harness printed.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Every regular file under `root`, sorted, so a failure names a stable case.
///
/// There is no filter and no exclusion list: a corpus directory holds inputs and
/// nothing else, so documentation and the seed generator live one level up in
/// `tests/fuzz/`. A driver that skips files by name is a driver that can be made
/// to skip the crash input.
std::vector<std::filesystem::path> corpus_files(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

/// libFuzzer never hands a target a null pointer, so neither does this — a
/// zero-length seed would otherwise construct `string_view(nullptr, 0)` and trip
/// UBSan's nonnull check, reporting the driver as a finding in the target.
int feed(const std::vector<std::uint8_t>& data) {
    static const std::uint8_t empty = 0;
    return LLVMFuzzerTestOneInput(data.empty() ? &empty : data.data(), data.size());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <corpus-dir-or-file> ...\n", argv[0]);
        return 2;
    }

    std::size_t inputs = 0;
    std::size_t bytes = 0;

    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path arg{argv[i]};
        std::error_code ec;

        if (std::filesystem::is_directory(arg, ec)) {
            const auto files = corpus_files(arg);
            if (files.empty()) {
                // An empty corpus directory is a defect, not a pass: REQ-SEC-011
                // requires seeds to exist, and a silent zero-case run is exactly
                // how a gate stops guarding anything.
                std::fprintf(stderr, "replay: %s contains no corpus inputs\n",
                             arg.string().c_str());
                return 1;
            }
            for (const auto& file : files) {
                const auto data = read_file(file);
                std::fprintf(stderr, "replay: %s (%zu bytes)\n",
                             file.filename().string().c_str(), data.size());
                std::fflush(stderr);
                (void)feed(data);
                ++inputs;
                bytes += data.size();
            }
            continue;
        }

        if (!std::filesystem::exists(arg, ec)) {
            std::fprintf(stderr, "replay: %s does not exist\n", arg.string().c_str());
            return 1;
        }
        const auto data = read_file(arg);
        std::fprintf(stderr, "replay: %s (%zu bytes)\n", arg.string().c_str(), data.size());
        std::fflush(stderr);
        (void)feed(data);
        ++inputs;
        bytes += data.size();
    }

    std::fprintf(stderr, "replay: %zu input(s), %zu byte(s), no invariant violated\n",
                 inputs, bytes);
    return 0;
}
