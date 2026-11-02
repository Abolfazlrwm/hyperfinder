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
    // What the directory enumerator learned about one entry.
    //
    // `name` and `full` are UTF-8 and only valid for the duration of the call
    // that receives this record: the Windows enumerator fills them from reused
    // buffers so a scan does not allocate two strings per entry.
    //
    // The type fields describe what the entry *is* after symlink resolution,
    // which is the enumerator's job because only it knows whether the entry was
    // a reparse point and whether --follow-symlinks was asked for.
    struct EntryRecord {
        std::string_view name;
        std::string_view full;
        bool directory = false;
        bool regular_file = false;
        bool link = false;
        bool hidden = false;
        // True when `size` and `mtime_seconds` came from the enumerator, so the
        // filters can be applied without another stat call.
        bool metadata_known = false;
        std::uintmax_t size = 0;
        std::int64_t mtime_seconds = 0;
    };

    // depth is the level of `path` itself: 0 for the root, 1 for its children.
    void scanDirectory(const fs::path& path, std::size_t depth);

    // Enumerates one directory and hands every entry to considerEntry. There is
    // one implementation per platform: FindFirstFileExW on Windows, a
    // directory_iterator elsewhere. Both only decide *how to list* a directory;
    // what to do with an entry lives in considerEntry alone.
    void enumerateInto(const fs::path& path, std::size_t depth, std::vector<std::string>& found);

    void submit(const fs::path& path, std::size_t depth);
    void recordError(std::exception_ptr error);
    bool nameMatches(std::string_view filename) const;

    // Applies the search rules to one entry: pruning, descending, counting and
    // collecting. Shared by the Windows and the POSIX enumerator so the two
    // cannot drift apart.
    void considerEntry(const EntryRecord& entry, std::size_t depth,
                       std::vector<std::string>& found);

    // Applies the --min-size/--max-size and --newer-than bounds to a regular
    // file. Both stats are only taken when the corresponding filter is active
    // and the enumerator did not already supply them, so an unfiltered search
    // pays nothing for them.
    bool passesSizeAndTime(const EntryRecord& entry);

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

    // True when options.newer_than_enabled is set. The cutoff is stored as whole
    // seconds on the file clock: a nanosecond file_time_type cannot represent
    // dates past ~2262 (int64 nanoseconds overflow), and a year-2999 cutoff is a
    // reasonable thing to ask for. Comparing in seconds sidesteps that.
    bool has_cutoff_ = false;
    std::int64_t cutoff_seconds_ = 0;

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
