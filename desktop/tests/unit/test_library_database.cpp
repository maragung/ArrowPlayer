// SPDX-License-Identifier: MPL-2.0
#include <filesystem>
#include <fstream>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#elif defined(_WIN32)
#include <process.h>
#endif

#include "library/filesystem_scanner.hpp"
#include "library/library_database.hpp"
#include "library/library_importer.hpp"
#include "library/sidecar_tag_reader.hpp"

#include <gtest/gtest.h>

namespace {

class LibraryDatabaseTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Each test instance gets its own path so the parallel ctest run
        // (--parallel 4) does not race on the same sqlite file. The base
        // name carries the test id, and a per-pid suffix keeps parallel
        // gtest binaries from colliding when several test executables are
        // themselves scheduled in parallel.
        const auto base =
            std::filesystem::temp_directory_path() / "eclipse-player-library-test";
        path = base.string() + "-" + ::testing::UnitTest::GetInstance()
                                       ->current_test_info()
                                       ->name() +
               "-" + std::to_string(static_cast<long>(getpid())) + ".sqlite";
        std::filesystem::remove(path);
    }

    void TearDown() override { std::filesystem::remove(path); }

    std::filesystem::path path;
};

TEST(FilesystemScanner, FindsSupportedFilesBoundedAndSorted) {
    const auto root = std::filesystem::temp_directory_path() / "eclipse-player-scan-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    std::ofstream(root / "z.mp3").put('x');
    std::ofstream(root / "nested" / "a.FLAC").put('x');
    std::ofstream(root / "ignore.txt").put('x');
    eclipse::library::FilesystemScanner scanner;
    const auto result = scanner.scan({root, 10});
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 2U);
    EXPECT_LT(result->at(0).string(), result->at(1).string());
    std::filesystem::remove_all(root);
}

TEST(FilesystemScanner, ImportsScannedFilesIdempotently) {
    const auto root = std::filesystem::temp_directory_path() / "eclipse-player-import-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "song.mp3").put('x');
    eclipse::library::FilesystemScanner scanner;
    eclipse::library::LibraryDatabase db;
    const auto database_path = root / "library.sqlite";
    ASSERT_TRUE(db.open(database_path));
    eclipse::library::SidecarTagReader tags;
    ASSERT_TRUE(eclipse::library::LibraryImporter::import_files({root, 10}, scanner, tags, db));
    ASSERT_TRUE(eclipse::library::LibraryImporter::import_files({root, 10}, scanner, tags, db));
    const auto tracks = db.list_tracks();
    ASSERT_TRUE(tracks);
    ASSERT_EQ(tracks->size(), 1U);
    EXPECT_EQ(tracks->front().title, "song");
    db.close();
    std::filesystem::remove_all(root);
}

TEST(SidecarTagReader, ReadsOptionalMetadataAndRejectsInvalidDuration) {
    const auto root = std::filesystem::temp_directory_path() / "eclipse-player-tags-test.mp3";
    std::ofstream(root).put('x');
    std::ofstream(root.string() + ".eclipse-tags")
        << "title=Custom Title\nartist=Artist\nduration_ms=1234\n";
    eclipse::library::SidecarTagReader reader;
    const auto result = reader.read(root);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->title, "Custom Title");
    EXPECT_EQ(result->artist, "Artist");
    EXPECT_EQ(result->duration_ms, 1234);
    std::ofstream(root.string() + ".eclipse-tags") << "duration_ms=-1\n";
    const auto invalid = reader.read(root);
    EXPECT_FALSE(invalid);
    std::filesystem::remove(root);
    std::filesystem::remove(root.string() + ".eclipse-tags");
}

TEST(FilesystemScanner, RejectsInvalidRequests) {
    eclipse::library::FilesystemScanner scanner;
    EXPECT_EQ(scanner.scan({{}, 10}).error().code(), eclipse::ErrorCode::InvalidArgument);
    EXPECT_EQ(scanner.scan({std::filesystem::temp_directory_path(), 0}).error().code(),
              eclipse::ErrorCode::InvalidArgument);
}

TEST_F(LibraryDatabaseTest, RequiresOpenBeforeInsert) {
    eclipse::library::LibraryDatabase db;
    const auto result = db.insert_track({"song.mp3", "Song", "Artist", 1000});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), eclipse::ErrorCode::InvalidState);
}

TEST_F(LibraryDatabaseTest, CreatesSchemaAndAcceptsMetadata) {
    eclipse::library::LibraryDatabase db;
    ASSERT_TRUE(db.open(path));
    EXPECT_TRUE(db.insert_track({"song.mp3", "Song", "Artist", 1000}));
}

TEST_F(LibraryDatabaseTest, ListsTracksInInsertionOrder) {
    eclipse::library::LibraryDatabase db;
    ASSERT_TRUE(db.open(path));
    ASSERT_TRUE(db.insert_track({"b.mp3", "B", "Artist", 2000}));
    ASSERT_TRUE(db.insert_track({"a.mp3", "A", "Artist", 1000}));
    const auto result = db.list_tracks();
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ(result->at(0).path, "b.mp3");
    EXPECT_EQ(result->at(1).title, "A");
}

TEST_F(LibraryDatabaseTest, RemovesTrackAndReportsMissingPath) {
    eclipse::library::LibraryDatabase db;
    ASSERT_TRUE(db.open(path));
    ASSERT_TRUE(db.insert_track({"song.mp3", "Song", "Artist", 1000}));
    ASSERT_TRUE(db.remove_track("song.mp3"));
    ASSERT_TRUE(db.list_tracks());
    EXPECT_TRUE(db.list_tracks()->empty());
    const auto missing = db.remove_track("song.mp3");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), eclipse::ErrorCode::FileNotFound);
}

TEST_F(LibraryDatabaseTest, RejectsDuplicateAndInvalidTracks) {
    eclipse::library::LibraryDatabase db;
    ASSERT_TRUE(db.open(path));
    ASSERT_TRUE(db.insert_track({"song.mp3", "Song", "Artist", 1000}));
    const auto duplicate = db.insert_track({"song.mp3", "Other", "Other", 2000});
    EXPECT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), eclipse::ErrorCode::ConstraintViolation);
    const auto invalid = db.insert_track({"", "", "", -1});
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), eclipse::ErrorCode::InvalidArgument);
}

TEST_F(LibraryDatabaseTest, CloseIsIdempotentAndReopenResetsHandle) {
    eclipse::library::LibraryDatabase db;
    ASSERT_TRUE(db.open(path));
    ASSERT_TRUE(db.insert_track({"before.mp3", "Before", "", 1}));
    EXPECT_TRUE(db.is_open());
    db.close();
    EXPECT_FALSE(db.is_open());
    db.close();
    ASSERT_TRUE(db.open(path));
    const auto tracks = db.list_tracks();
    ASSERT_TRUE(tracks);
    ASSERT_EQ(tracks->size(), 1U);
    EXPECT_EQ(tracks->front().path, "before.mp3");
}

TEST_F(LibraryDatabaseTest, RejectsEmptyDatabasePathWithoutOpening) {
    eclipse::library::LibraryDatabase db;
    EXPECT_FALSE(db.open({}));
    EXPECT_FALSE(db.is_open());
}

TEST_F(LibraryDatabaseTest, RequiresOpenForQueries) {
    eclipse::library::LibraryDatabase db;
    const auto tracks = db.list_tracks();
    ASSERT_FALSE(tracks);
    EXPECT_EQ(tracks.error().code(), eclipse::ErrorCode::InvalidState);
    const auto removed = db.remove_track("song.mp3");
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().code(), eclipse::ErrorCode::InvalidState);
}

}  // namespace
