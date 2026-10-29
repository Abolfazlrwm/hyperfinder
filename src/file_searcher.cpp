#include "file_searcher.h"
#include <iostream>
#include <algorithm>

FileSearcher::FileSearcher(const std::string& q, const fs::path& root) 
    : query(toLower(q)), rootPath(root) {}

std::string FileSearcher::toLower(std::string s) const {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

bool FileSearcher::containsQuery(const std::string& filename) const {
    return toLower(filename).find(query) != std::string::npos;
}

void FileSearcher::scanDirectory(const fs::path& path) {
    try {
        if (!fs::exists(path) || !fs::is_directory(path)) return;

        for (const auto& entry : fs::directory_iterator(path)) {
            scannedFilesCount++;
            if (fs::is_directory(entry)) {
                // ارسال دایرکتوری‌های جدید به Thread Pool برای پردازش موازی
                pool.enqueue([this, p = entry.path()] { scanDirectory(p); });
            } else if (fs::is_regular_file(entry)) {
                if (containsQuery(entry.path().filename().string())) {
                    std::lock_guard<std::mutex> lock(results_mutex);
                    matches.push_back(entry.path().string());
                }
            }
        }
    } catch (const std::exception& e) {
        // نادیده گرفتن خطاهای دسترسی (Permission Denied) برای جلوگیری از توقف برنامه
    }
}

void FileSearcher::search() {
    pool.enqueue([this, root = rootPath] { scanDirectory(root); });
    // Every directory task spawns further tasks for its subdirectories, so the
    // pool is only idle once the whole tree has been visited.
    pool.waitIdle();
}

void FileSearcher::displayResults() const {
    std::lock_guard<std::mutex> lock(results_mutex);
    if (matches.empty()) {
        std::cout << "\nNo matches found.\n";
    } else {
        std::cout << "\nFound matches:\n";
        for (const auto& path : matches) {
            std::cout << path << "\n";
        }
    }
}

uint64_t FileSearcher::getScannedCount() const {
    return scannedFilesCount.load();
}