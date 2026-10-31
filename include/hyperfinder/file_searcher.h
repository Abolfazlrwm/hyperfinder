#ifndef HYPERFINDER_FILE_SEARCHER_H
#define HYPERFINDER_FILE_SEARCHER_H

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <hyperfinder/options.h>
#include <hyperfinder/thread_pool.h>

namespace hyperfinder {

namespace fs = std::filesystem;

// What the last search() actually looked at. Directories are counted
// separately from files so "files scanned" means regular files only.
struct SearchStats {
    std::uint64_t files_scanned = 0;
    std::uint64_t directories_scanned = 0;
    std::uint64_t paths_skipped = 0;

    // Wall-clock duration of the last search(), measured around the scan only.
    double elapsed_seconds = 0.0;
};

// One line of a file whose contents matched --content. `line` is 1-based and
// `text` is the line with a trailing CR removed and, if it is very long,
// truncated to a printable width (see the implementation).
struct ContentMatch {
    std::string path;
    std::size_t line = 0;
    std::string text;
};

// Scans a directory tree and collects the paths that match.
//
// The class never writes to a stream. Everything it produces is returned
// through results() and stats(), which keeps the search testable without
// capturing stdout and lets the executable decide on formatting and colour.
class FileSearcher {
public:
    // The full configuration, as produced by the command-line layer.
    explicit FileSearcher(Options options);

    // Convenience overload for the common case; everything else keeps its
    // default.
    FileSearcher(const std::string& query, const fs::path& root);

    // Scans the tree and returns only after every spawned task has finished.
    // Throws if the root cannot be read or if a task failed unexpectedly.
    void search();

    // Matches from the last search(), sorted. Safe to call once search() has
    // returned; not safe to call while a search is running.
    const std::vector<std::string>& results() const noexcept;

    // The matching lines from the last content search, sorted by path and then
    // by line number. Empty unless options.content was set.
    const std::vector<ContentMatch>& contentMatches() const noexcept;

    SearchStats stats() const;
    std::uint64_t getScannedCount() const;
    std::uint64_t getSkippedCount() const;

private:
    // depth is the level of `path` itself: 0 for the root, 1 for its children.
    void scanDirectory(const fs::path& path, std::size_t depth);
    void submit(const fs::path& path, std::size_t depth);
    void recordError(std::exception_ptr error);
    bool nameMatches(const std::string& filename) const;

    // Applies the --min-size/--max-size and --newer-than bounds to a regular
    // file. Both stats are only taken when the corresponding filter is active,
    // so an unfiltered search pays nothing for them.
    bool passesSizeAndTime(const fs::path& path);

    // Reads `path` in chunks and appends one ContentMatch per matching line.
    // Runs on a pool thread; merges its results under results_mutex.
    void scanContent(const fs::path& path);

    // Enqueues scanContent(path) as its own pool task.
    void submitContent(const fs::path& path);

    // Returns true when `directory` was already reached during this search, and
    // records it otherwise. Only used with follow_symlinks, where a link can
    // point back at an ancestor.
    bool markVisited(const fs::path& directory);

    // Declared before the members that read it, because member initialisation
    // follows declaration order.
    Options options;

    // options.query, lower-cased once when ignore_case is on, so the hot path
    // does not re-fold the needle for every file.
    std::string query;
    MatchMode match_mode_;
    // Compiled pattern, valid only when match_mode_ == MatchMode::Regex. Built in
    // the constructor; an invalid pattern throws std::regex_error, which main()
    // turns into exit code 2.
    std::regex regex_;

    // True when options.newer_than_enabled is set. The cutoff is an absolute
    // file-clock instant: relative windows are measured back from "now", while
    // an ISO date is converted through the system clock (see file_searcher.cpp).
    bool has_cutoff_ = false;
    fs::file_time_type cutoff_{};

    fs::path rootPath;
    std::vector<std::string> matches;
    std::vector<ContentMatch> content_matches_;
    // options.content, lower-cased once when ignore_case is on.
    std::string content_needle_;
    mutable std::mutex results_mutex;

    std::atomic<std::uint64_t> filesScanned{0};
    std::atomic<std::uint64_t> directoriesScanned{0};
    std::atomic<std::uint64_t> skippedPaths{0};
    std::atomic<double> elapsedSeconds{0.0};

    std::mutex error_mutex;
    std::exception_ptr firstError;

    std::mutex visited_mutex;
    std::unordered_set<std::string> visited_directories;

    ThreadPool pool;
};

}  // namespace hyperfinder

#endif  // HYPERFINDER_FILE_SEARCHER_H
