// SPDX-License-Identifier: MPL-2.0
#include <filesystem>
#include <fstream>

#include "library/filesystem_scanner.hpp"
#include "library/sidecar_tag_reader.hpp"

#include <gtest/gtest.h>

TEST(LibraryRobustness, ScannerReportsMissingRoot) {
    eclipse::library::FilesystemScanner scanner;
    const auto missing = std::filesystem::temp_directory_path() / "eclipse-player-no-such-root";
    std::filesystem::remove_all(missing);
    const auto result = scanner.scan({missing, 10});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), eclipse::ErrorCode::NotADirectory);
}

TEST(LibraryRobustness, SidecarUnknownFieldsAreIgnored) {
    const auto media = std::filesystem::temp_directory_path() / "eclipse-player-unknown.mp3";
    const auto sidecar = media.string() + ".eclipse-tags";
    std::ofstream(media).put('x');
    std::ofstream(sidecar) << "unknown=ignored\ntitle=Known\n";
    eclipse::library::SidecarTagReader reader;
    const auto result = reader.read(media);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->title, "Known");
    EXPECT_EQ(result->artist, "");
    std::filesystem::remove(media);
    std::filesystem::remove(sidecar);
}

TEST(LibraryRobustness, SidecarSizeIsBounded) {
    const auto media = std::filesystem::temp_directory_path() / "eclipse-player-large.mp3";
    const auto sidecar = media.string() + ".eclipse-tags";
    std::ofstream(media).put('x');
    std::ofstream output(sidecar);
    output << "title=" << std::string(65 * 1024, 'x');
    output.close();
    eclipse::library::SidecarTagReader reader;
    const auto result = reader.read(media);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), eclipse::ErrorCode::InputTooLarge);
    std::filesystem::remove(media);
    std::filesystem::remove(sidecar);
}
