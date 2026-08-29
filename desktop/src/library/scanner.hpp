// SPDX-License-Identifier: MPL-2.0
// Arrow Player — Filesystem scanner
// Spec: §7.5 (library scan data flow), §9.4, REQ-LIB-001
//
// Filesystem scanner that:
//   - Walks one or more library roots
//   - Classifies files as NEW / CHANGED / UNCHANGED / GONE
//   - Detects symlink loops via device+inode tracking
//   - Debounces filesystem events (1000ms)
//   - Reports progress throttled to 10 Hz
//   - Supports inotify (Linux) / ReadDirectoryChangesW (Windows) watching
//
// Usage:
//   Scanner scanner;
//   scanner.set_database(&db);
//   auto result = scanner.scan(request);
//   scanner.start_watching({"/path/to/music"});
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "core/error.hpp"
#include "library/database.hpp"

namespace arrow::library {

// Forward declare record types from database.hpp
struct TrackRecord;

// ===========================================================================
//  File identity
// ===========================================================================

/// Device + inode pair for symlink-loop detection (§7.5).
struct FileIdentity final {
    uint64_t device{0};
    uint64_t inode{0};

    [[nodiscard]] bool operator==(const FileIdentity& other) const noexcept {
        return device == other.device && inode == other.inode;
    }
};

}  // namespace arrow::library

// Inject hash support into std
template<>
struct std::hash<arrow::library::FileIdentity> {
    [[nodiscard]] std::size_t operator()(
        const arrow::library::FileIdentity& id) const noexcept {
        return std::hash<uint64_t>{}(id.device) ^ (std::hash<uint64_t>{}(id.inode) << 1);
    }
};

namespace arrow::library {

// ===========================================================================
//  Scan phases and progress
// ===========================================================================

enum class ScanPhase {
    Discovering,   ///< walking the filesystem tree
    Diffing,      ///< comparing against the database
    Writing,      ///< writing new/changed tracks to DB
    Complete,     ///< scan finished
    Error,        ///< scan failed
    FileCreated,  ///< incremental: a new file appeared
    FileChanged,  ///< incremental: an existing file was modified
    FileDeleted,  ///< incremental: a file was deleted
};

/// Progress update sent to the UI during a scan.
struct ScanProgress final {
    ScanPhase phase{ScanPhase::Discovering};
    int64_t files_scanned{0};
    int64_t files_total{0};
    std::string current_path;
    std::string error_message;
};

/// Callback type for progress updates. Called on a scanner-internal thread;
/// throttled to 10 Hz. Must not throw.
using ScanProgressCallback = std::function<void(const ScanProgress&)>;

// ===========================================================================
//  File events (watch mode)
// ===========================================================================

enum class FileEvent {
    Created,
    Modified,
    Deleted,
};

/// Callback for filesystem watch events. Called from an internal thread;
/// events are debounced per path.
using FileWatchCallback = std::function<void(const std::filesystem::path&, FileEvent)>;

// ===========================================================================
//  Scan request
// ===========================================================================

/// Parameters for a full library scan.
struct ScanRequest final {
    /// Primary library root. Must be a non-empty, accessible directory.
    std::filesystem::path root;
    /// Additional roots to include in the same scan.
    std::vector<std::filesystem::path> additional_roots;
    /// Maximum number of files to scan. 0 means unlimited.
    std::size_t max_files{100000};
    /// If true, scans even files that have not changed (forces re-read of tags).
    bool force_rescan{false};

    explicit ScanRequest(std::filesystem::path root,
                       std::vector<std::filesystem::path> additional_roots = {});
};

// ===========================================================================
//  Scan result
// ===========================================================================

/// Outcome of a full library scan.
struct ScanStats final {
    int total_discovered{0};
    int total_existing{0};
    int new_count{0};
    int changed_count{0};
    int gone_count{0};
    int written_count{0};
};

/// Outcome of a library scan. `error` is set on failure.
struct ScanResult final {
    std::vector<std::filesystem::path> new_files;
    std::vector<std::filesystem::path> changed_files;
    std::vector<std::filesystem::path> unchanged_files;
    std::vector<std::string> gone_files;  // string paths (soft-delete candidates)
    ScanStats stats;
    std::optional<Error> error;

    [[nodiscard]] static ScanResult ok();
    [[nodiscard]] static ScanResult error(Error e);
    [[nodiscard]] bool ok() const noexcept {
        return !error.has_value();
    }
};

// ===========================================================================
//  Discovered file
// ===========================================================================

/// A single file discovered during filesystem walk.
struct DiscoveredFile final {
    std::filesystem::path path;
    FileIdentity file_identity;
    int64_t file_size{0};
    time_t mtime{0};
};

// ===========================================================================
//  Scanner
// ===========================================================================

/// Filesystem scanner and watcher.
///
/// Thread-safety: all public methods are safe to call from any thread.
/// Internal state is protected by a mutex.
class Scanner final {
  public:
    Scanner();
    ~Scanner();

    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;

    // -----------------------------------------------------------------------
    //  Configuration
    // -----------------------------------------------------------------------

    /// Set the database to diff against and write to.
    void set_database(Database* db);

    /// Set the progress callback. Must not throw.
    void set_progress_callback(ScanProgressCallback cb);

    // -----------------------------------------------------------------------
    //  Full scan
    // -----------------------------------------------------------------------

    /// Perform a full library scan of the requested roots.
    ///
    /// Phases:
    ///   1. Discover: walk all roots, collect supported audio files
    ///   2. Diff: compare against database, classify as NEW/CHANGED/UNCHANGED/GONE
    ///   3. Write: upsert new and changed tracks (caller drives this step)
    ///
    /// The `ScanResult` gives the caller the list of files to process;
    /// this scanner does not write tags — that is the `TagReader`'s job.
    [[nodiscard]] Result<ScanResult> scan(const ScanRequest& request) const;

    // -----------------------------------------------------------------------
    //  File watching
    // -----------------------------------------------------------------------

    /// Start watching the given roots for changes.
    /// Uses inotify on Linux, ReadDirectoryChangesW on Windows.
    void start_watching(const std::vector<std::filesystem::path>& roots);

    /// Stop watching.
    void stop_watching();

    // -----------------------------------------------------------------------
    //  Symlink loop tracking
    // -----------------------------------------------------------------------

    /// Clear the device+inode seen set. Call before a fresh scan to reset
    /// symlink loop detection.
    void clear_seen_identities() {
        std::unique_lock lock{seen_identity_mutex_};
        seen_identities_.clear();
    }

  private:
    /// Classify discovered files against the database.
    [[nodiscard]] Result<ScanResult> diff(
        const std::vector<DiscoveredFile>& discovered) const;

    /// Report progress, throttled to 10 Hz.
    void report_progress(ScanPhase phase, int64_t current, int64_t total,
                       std::string_view current_path = {});

    /// Handle a filesystem watch event.
    void on_file_event(const std::filesystem::path& path, FileEvent ev);

    // Internal state
    mutable std::shared_mutex mutex_;
    Database* db_{nullptr};
    ScanProgressCallback progress_callback_;

    // Symlink loop detection
    mutable std::shared_mutex seen_identity_mutex_;
    std::unordered_set<FileIdentity> seen_identities_;

    // Debounce
    struct Debouncer;
    std::unique_ptr<Debouncer> debouncer_;

    // Platform-specific watcher
    struct FileWatcher;
    std::unique_ptr<FileWatcher> watcher_;
};

// ===========================================================================
//  ScanJob — batched write helper for the scan pipeline
// ===========================================================================

/// Batches track writes to the database during a scan. All writes go through
/// the single writer thread via `Database::write()`.
class ScanJob final {
  public:
    /// Construct a scan job backed by `db`. Progress callback receives
    /// (written_count, total) as the scan progresses.
    explicit ScanJob(Database& db,
                    std::function<void(int, int)> progress_cb = nullptr);

    /// Add one track to the write batch.
    [[nodiscard]] Status add_track(const TrackRecord& track);

    /// Mark gone paths as soft-deleted and finalize the job.
    [[nodiscard]] Status commit(const std::vector<std::string>& gone_paths);

    [[nodiscard]] int written_count() const noexcept;

  private:
    Database& db_;
    std::function<void(int, int)> progress_cb_;
    int written_{0};
};

}  // namespace arrow::library
