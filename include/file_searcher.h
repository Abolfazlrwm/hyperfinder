#ifndef FILE_SEARCHER_H
#define FILE_SEARCHER_H

#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <atomic>
#include "thread_pool.h"

namespace fs = std::filesystem;

class FileSearcher {
public:
    FileSearcher(const std::string& query, const fs::path& root);
    void search();
    void displayResults() const;
    uint64_t getScannedCount() const;

private:
    void scanDirectory(const fs::path& path);
    bool containsQuery(const std::string& filename) const;
    std::string toLower(std::string s) const;

    std::string query;
    fs::path rootPath;
    std::vector<std::string> matches;
    mutable std::mutex results_mutex;
    std::atomic<uint64_t> scannedFilesCount{0};
    ThreadPool pool;
};

#endif
