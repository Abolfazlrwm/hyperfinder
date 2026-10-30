#ifndef HYPERFINDER_FILE_SEARCHER_H
#define HYPERFINDER_FILE_SEARCHER_H

#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <exception>
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
};

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

    void displayResults() const;

    // Matches from the last search(), sorted. Safe to call once search() has
    // returned; not safe to call while a search is running.
    const std::vector<std::string>& results() const noexcept;

    SearchStats stats() const;
    std::uint64_t getScannedCount() const;
    std::uint64_t getSkippedCount() const;

private:
    void scanDirectory(const fs::path& path);
    void submit(const fs::path& path);
    void recordError(std::exception_ptr error);
    bool containsQuery(const std::string& filename) const;

    // Declared before the members that read it, because member initialisation
    // follows declaration order.
    Options options;

    // options.query, lower-cased once when ignore_case is on, so the hot path
    // does not re-fold the needle for every file.
    std::string query;
    fs::path rootPath;
    std::vector<std::string> matches;
    mutable std::mutex results_mutex;

    std::atomic<std::uint64_t> filesScanned{0};
    std::atomic<std::uint64_t> directoriesScanned{0};
    std::atomic<std::uint64_t> skippedPaths{0};

    std::mutex error_mutex;
    std::exception_ptr firstError;
    ThreadPool pool;
};

}  // namespace hyperfinder

#endif  // HYPERFINDER_FILE_SEARCHER_H
