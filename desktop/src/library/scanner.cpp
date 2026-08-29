// SPDX-License-Identifier: MPL-2.0
#include "library/scanner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapi.h>
#else
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace arrow::library {

namespace {

// ---------------------------------------------------------------------------
//  Supported audio file extensions
// ---------------------------------------------------------------------------

constexpr std::array<const char*, 10> kAudioExtensions = {
    ".mp3", ".flac", ".ogg", ".opus", ".wav", ".m4a", ".aac",
    ".ogg", ".wma", ".alac",
};

[[nodiscard]] bool is_supported_extension(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    if (ext.empty()) return false;
    for (const char* e : kAudioExtensions) {
        if (ext.size() == std::strlen(e) &&
            std::equal(ext.begin(), ext.end(), e,
                       [](char a, char b) {
                           return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a))) ==
                                  static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b)));
                       })) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
//  File identity — device+inode for symlink-loop detection
// ---------------------------------------------------------------------------

#if defined(_WIN32)

[[nodiscard]] std::optional<FileIdentity> get_file_identity(
    const std::filesystem::path&) noexcept {
    // Windows: use file size + mtime as a proxy for identity.
    // True inode support requires BY_HANDLE_FILE_INFORMATION via CreateFile.
    std::error_code ec;
    auto ftime = std::chrono::clock_cast<std::chrono::system_clock>(
        std::filesystem::last_write_time(path, ec));
    if (ec) return std::nullopt;
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              ftime.time_since_epoch())
                              .count();
    return FileIdentity{0, static_cast<uint64_t>(epoch_ms)};
}

#else

[[nodiscard]] std::optional<FileIdentity> get_file_identity(
    const std::filesystem::path& path) noexcept {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return std::nullopt;
    return FileIdentity{st.st_dev, static_cast<uint64_t>(st.st_ino)};
}

#endif

// ---------------------------------------------------------------------------
//  Debounce map (path → last event timestamp)
// ---------------------------------------------------------------------------

class Debouncer {
  public:
    static constexpr std::chrono::milliseconds kDebounceInterval{1000};

    /// Returns true if the event should be propagated, false if it is
    /// debounced (too soon after the last event for this path).
    [[nodiscard]] bool should_fire(const std::filesystem::path& path) {
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);
        const auto it = last_event_.find(path);
        if (it != last_event_.end()) {
            if (now - it->second < kDebounceInterval) {
                return false;
            }
        }
        last_event_.insert_or_assign(path, now);
        return true;
    }

  private:
    std::mutex mutex_;
    std::unordered_map<std::filesystem::path, std::chrono::steady_clock::time_point> last_event_;
};

// ---------------------------------------------------------------------------
//  Progress reporting throttler
// ---------------------------------------------------------------------------

class ProgressThrottler {
  public:
    static constexpr auto kMinInterval = std::chrono::milliseconds{100};  // 10 Hz

    explicit ProgressThrottler(ScanProgressCallback cb)
          : callback_{std::move(cb)} {}

    void report(const ScanProgress& progress) {
        const auto now = std::chrono::steady_clock::now();
        std::unique_lock lock(mutex_);
        if (now - last_report_ >= kMinInterval || progress.phase == ScanPhase::Complete) {
            last_report_ = now;
            if (callback_) callback_(progress);
        }
    }

  private:
    std::chrono::steady_clock::time_point last_report_{};
    std::mutex mutex_;
    ScanProgressCallback callback_;
};

// ---------------------------------------------------------------------------
//  Platform-specific file watcher
// ---------------------------------------------------------------------------

#if defined(_WIN32)

class FileWatcher {
  public:
    FileWatcher() = default;
    ~FileWatcher() { stop(); }

    bool start(const std::vector<std::filesystem::path>& paths,
               FileWatchCallback callback) {
        stop();
        callback_ = std::move(callback);

        // Build a recursive watch for each root using ReadDirectoryChangesW.
        // On Windows we open each directory handle and poll via GetQueuedCompletionStatus.
        // For simplicity in this implementation we use a background thread that
        // polls modification times every 5 seconds (a reasonable fallback since
        // Windows file notification APIs require a message loop).
        running_ = true;
        thread_ = std::jthread([this, paths](std::stop_token token) {
            while (!token.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::seconds{5});
                if (token.stop_requested()) break;
                for (const auto& p : paths) {
                    std::error_code ec;
                    if (!std::filesystem::exists(p, ec)) continue;
                    notify_event(p, FileEvent::Modified);
                }
            }
        });
        return true;
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.request_stop();
    }

    void notify_event(const std::filesystem::path& path, FileEvent ev) {
        std::unique_lock lock(mutex_);
        if (callback_) callback_(path, ev);
    }

  private:
    std::atomic_bool running_{false};
    std::jthread thread_;
    FileWatchCallback callback_;
    std::mutex mutex_;
};

#else

// inotify-based watcher. Each directory gets its own inotify descriptor.
// Symlink loops are detected via device+inode tracking.
class FileWatcher {
  public:
    FileWatcher() {
        inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    }

    ~FileWatcher() { stop(); }

    bool is_valid() const noexcept { return inotify_fd_ >= 0; }

    bool add_watch(const std::filesystem::path& path) {
        if (!is_valid()) return false;
        const int wd =
            ::inotify_add_watch(inotify_fd_, path.c_str(),
                                IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO |
                                    IN_MOVE_SELF | IN_DELETE_SELF);
        if (wd < 0) return false;
        std::unique_lock lock(mutex_);
        watch_descriptors_[wd] = path;
        return true;
    }

    bool start(FileWatchCallback callback) {
        callback_ = std::move(callback);
        running_ = true;
        thread_ = std::jthread([this](std::stop_token token) {
            std::array<std::byte, 65536> buffer;
            while (!token.stop_requested()) {
                ssize_t n = ::read(inotify_fd_, buffer.data(), buffer.size());
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        std::this_thread::sleep_for(std::chrono::milliseconds{50});
                        continue;
                    }
                    break;
                }
                ssize_t offset = 0;
                while (offset < n) {
                    const auto* event =
                        reinterpret_cast<const inotify_event*>(buffer.data() + offset);
                    offset += sizeof(inotify_event) + event->len;

                    std::filesystem::path watched_path;
                    {
                        std::shared_lock lock(mutex_);
                        auto it = watch_descriptors_.find(event->wd);
                        if (it != watch_descriptors_.end()) watched_path = it->second;
                    }

                    FileEvent ev = FileEvent::Modified;
                    if (event->mask & (IN_CREATE | IN_MOVED_TO)) ev = FileEvent::Created;
                    else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) ev = FileEvent::Deleted;

                    if (!watched_path.empty() && event->len > 0) {
                        std::filesystem::path full = watched_path / event->name;
                        std::unique_lock lock(callback_mutex_);
                        if (callback_) callback_(full, ev);
                    }
                }
            }
        });
        return true;
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.request_stop();
        if (is_valid()) {
            ::close(inotify_fd_);
            inotify_fd_ = -1;
        }
        std::unique_lock lock(mutex_);
        watch_descriptors_.clear();
    }

  private:
    int inotify_fd_{-1};
    std::atomic_bool running_{false};
    std::jthread thread_;
    FileWatchCallback callback_;
    std::mutex callback_mutex_;
    std::mutex mutex_;
    std::unordered_map<int, std::filesystem::path> watch_descriptors_;
};

#endif

}  // anonymous namespace

// ===========================================================================
//  ScanRequest
// ===========================================================================

ScanRequest::ScanRequest(std::filesystem::path root,
                         std::vector<std::filesystem::path> additional_roots)
      : root{std::move(root)},
        additional_roots{std::move(additional_roots)} {}

// ===========================================================================
//  ScanResult
// ===========================================================================

ScanResult ScanResult::ok() { return ScanResult{{}, {}, {}, {}}; }

ScanResult ScanResult::error(Error e) {
    ScanResult r;
    r.error = std::move(e);
    return r;
}

// ===========================================================================
//  Scanner
// ===========================================================================

Scanner::Scanner() : debouncer_{std::make_unique<Debouncer>()} {}

Scanner::~Scanner() = default;

void Scanner::set_progress_callback(ScanProgressCallback cb) {
    std::unique_lock lock(mutex_);
    progress_callback_ = std::move(cb);
}

void Scanner::report_progress(ScanPhase phase, int64_t current, int64_t total,
                             std::string_view current_path) {
    std::unique_lock lock(mutex_);
    if (!progress_callback_) return;
    ProgressThrottler throttler{progress_callback_};
    ScanProgress prog;
    prog.phase = phase;
    prog.files_scanned = current;
    prog.files_total = total;
    if (!current_path.empty()) {
        prog.current_path = std::string{current_path};
    }
    throttler.report(prog);
}

Result<ScanResult> Scanner::scan(const ScanRequest& request) const {
    // Collect all roots
    std::vector<std::filesystem::path> roots;
    if (!request.root.empty()) roots.push_back(request.root);
    roots.insert(roots.end(), request.additional_roots.begin(),
                 request.additional_roots.end());

    if (roots.empty()) {
        return err(ErrorCode::InvalidArgument, "No library roots specified.");
    }

    // Validate roots
    for (const auto& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec) {
            return err(ErrorCode::NotADirectory,
                       "Library root is not accessible: " + root.string());
        }
    }

    // Phase 1: collect all audio file paths
    report_progress(ScanPhase::Discovering, 0, 0, {});

    std::vector<DiscoveredFile> discovered;
    discovered.reserve(request.max_files);

    int64_t total = 0;
    for (const auto& root : roots) {
        std::error_code ec;
        std::filesystem::directory_options options =
            std::filesystem::directory_options::skip_permission_denied;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root, options, ec)) {
            if (ec && ec != std::errc::permission_denied) break;

            std::error_code entry_ec;
            if (!entry.is_regular_file(entry_ec) || entry_ec) continue;
            if (!is_supported_extension(entry.path())) continue;

            auto identity = get_file_identity(entry.path());
            if (!identity) continue;

            // Symlink loop detection: skip if we've seen this device+inode
            {
                std::shared_lock lock{seen_identity_mutex_};
                if (seen_identities_.count(*identity)) continue;
            }

            DiscoveredFile f;
            f.path = entry.path();
            f.file_identity = *identity;
            f.file_size = entry.file_size(entry_ec);

            std::error_code mtime_ec;
            f.mtime = std::chrono::system_clock::to_time_t(
                std::chrono::clock_cast<std::chrono::system_clock>(
                    entry.last_write_time(mtime_ec)));

            discovered.push_back(std::move(f));
            {
                std::shared_lock lock{seen_identity_mutex_};
                seen_identities_.insert(*identity);
            }
            ++total;
            if (total % 1000 == 0) {
                report_progress(ScanPhase::Discovering, total, total, f.path);
            }
            if (total >= static_cast<int64_t>(request.max_files)) break;
        }
        if (total >= static_cast<int64_t>(request.max_files)) break;
    }

    report_progress(ScanPhase::Discovering, total, total, {});

    // Phase 2: diff against the database
    report_progress(ScanPhase::Diffing, 0, total, {});
    auto diff_result = diff(discovered);
    if (!diff_result) return diff_result.error();

    ScanResult result = std::move(diff_result).value();
    result.stats.total_discovered = static_cast<int>(discovered.size());
    report_progress(ScanPhase::Complete, result.stats.total_discovered,
                   result.stats.total_discovered, {});

    return std::move(result);
}

Result<ScanResult> Scanner::diff(const std::vector<DiscoveredFile>& discovered) const {
    if (!db_) {
        return err(ErrorCode::InvalidState,
                   "No database connection set on the scanner.");
    }

    // Get all existing paths from DB
    auto existing_paths = db_->get_all_paths();
    if (!existing_paths) return existing_paths.error();

    std::unordered_set<std::string> existing_set{existing_paths->begin(),
                                                 existing_paths->end()};

    ScanResult result;
    result.stats.total_discovered = static_cast<int>(discovered.size());
    result.stats.total_existing = static_cast<int>(existing_paths->size());

    // Classify each discovered file
    for (std::size_t i = 0; i < discovered.size(); ++i) {
        const auto& f = discovered[i];
        const auto path_str = f.path.generic_string();

        auto it = existing_set.find(path_str);
        if (it == existing_set.end()) {
            result.new_files.push_back(f.path);
        } else {
            // Check if file has changed (size or mtime)
            auto existing = db_->get_track_by_path(path_str);
            if (!existing || existing->file_size != f.file_size ||
                existing->updated_at < f.mtime) {
                result.changed_files.push_back(f.path);
            } else {
                result.unchanged_files.push_back(f.path);
            }
            existing_set.erase(it);
        }

        if (i % 100 == 0) {
            report_progress(ScanPhase::Diffing, static_cast<int64_t>(i),
                           static_cast<int64_t>(discovered.size()), f.path);
        }
    }

    // Remaining in existing_set are gone files
    result.gone_files = std::vector<std::string>{existing_set.begin(), existing_set.end()};
    std::sort(result.gone_files.begin(), result.gone_files.end());

    result.stats.new_count = static_cast<int>(result.new_files.size());
    result.stats.changed_count = static_cast<int>(result.changed_files.size());
    result.stats.gone_count = static_cast<int>(result.gone_files.size());

    return std::move(result);
}

void Scanner::set_database(Database* db) {
    std::unique_lock lock(mutex_);
    db_ = db;
}

void Scanner::start_watching(const std::vector<std::filesystem::path>& roots) {
    std::unique_lock lock(mutex_);
    if (watcher_) {
        watcher_->stop();
        watcher_.reset();
    }

#if !defined(_WIN32)
    auto* fw = new FileWatcher{};
    for (const auto& r : roots) fw->add_watch(r);
    watcher_.reset(fw);
    fw->start([this](const std::filesystem::path& path, FileEvent ev) {
        on_file_event(path, ev);
    });
#endif
}

void Scanner::stop_watching() {
    std::unique_lock lock(mutex_);
    if (watcher_) {
        watcher_->stop();
        watcher_.reset();
    }
}

void Scanner::on_file_event(const std::filesystem::path& path, FileEvent ev) {
    // Debounce
    if (!debouncer_->should_fire(path)) return;

    ScanProgressCallback cb;
    {
        std::shared_lock lock(mutex_);
        cb = progress_callback_;
    }
    if (!cb) return;

    ScanProgress prog;
    switch (ev) {
        case FileEvent::Created:   prog.phase = ScanPhase::FileCreated; break;
        case FileEvent::Modified:  prog.phase = ScanPhase::FileChanged; break;
        case FileEvent::Deleted:   prog.phase = ScanPhase::FileDeleted; break;
    }
    prog.current_path = path.generic_string();
    cb(prog);
}

// ===========================================================================
//  ScanJob — batched DB write helper
// ============================================================================

ScanJob::ScanJob(Database& db, std::function<void(int, int)> progress_cb)
      : db_{db}, progress_cb_{std::move(progress_cb)} {}

Status ScanJob::add_track(const TrackRecord& track) {
    if (auto res = db_.upsert_track(track); !res) return res;
    ++written_;
    if (progress_cb_) progress_cb_(written_, 0);
    return ok();
}

Status ScanJob::commit(const std::vector<std::string>& gone_paths) {
    if (auto res = db_.mark_paths_gone(gone_paths); !res) return res;
    return ok();
}

int ScanJob::written_count() const noexcept { return written_; }

}  // namespace arrow::library
