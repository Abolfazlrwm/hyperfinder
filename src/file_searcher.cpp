#include "file_searcher.h"
#include <iostream>
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// True when the entry is a directory link rather than a real directory.
//
// On Windows, libstdc++ implements symlink_status() by querying the target, so
// both symbolic links and junctions are reported as plain directories and
// std::filesystem cannot see the reparse point at all. Ask Win32 directly in
// that case; on POSIX the standard library check is enough.
//
// The Win32 call is only made for entries that already look like directories:
// it costs a syscall per entry, which measured ~4x slower on a 2624-entry tree
// when applied to files as well. Only directory links can create a cycle.
bool is_directory_link(const fs::directory_entry& entry, const fs::file_status& link_status) {
    if (fs::is_symlink(link_status)) return true;
#if defined(_WIN32)
    const DWORD attributes = ::GetFileAttributesW(entry.path().c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)entry;
    return false;
#endif
}

}  // namespace

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
    // skip_permission_denied asks the iterator to walk past entries it is not
    // allowed to read instead of throwing. All remaining filesystem calls use
    // the error_code overloads, so nothing escapes as an exception.
    std::error_code ec;
    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        skippedPaths.fetch_add(1);
        return;
    }

    directoriesScanned.fetch_add(1);

    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;

        std::error_code entry_ec;
        const fs::file_status link_status = entry.symlink_status(entry_ec);
        if (entry_ec) {
            skippedPaths.fetch_add(1);
        } else if (fs::is_directory(link_status)) {
            // Links are not followed: descending into them can loop forever on
            // a cycle and can leave the requested tree. --follow-symlinks with
            // a cycle guard is on the roadmap.
            if (!is_directory_link(entry, link_status)) {
                submit(entry.path());
            }
        } else if (fs::is_regular_file(link_status)) {
            filesScanned.fetch_add(1);
            if (containsQuery(entry.path().filename().string())) {
                std::lock_guard<std::mutex> lock(results_mutex);
                matches.push_back(entry.path().string());
            }
        }

        it.increment(ec);
        if (ec) {
            skippedPaths.fetch_add(1);
            break;
        }
    }
}

void FileSearcher::submit(const fs::path& path) {
    pool.enqueue([this, path] {
        try {
            scanDirectory(path);
        } catch (...) {
            // Without this the exception would land in a future nobody reads
            // and the subtree would silently disappear. Store the first one and
            // rethrow it from search() instead.
            recordError(std::current_exception());
        }
    });
}

void FileSearcher::recordError(std::exception_ptr error) {
    std::lock_guard<std::mutex> lock(error_mutex);
    if (!firstError) firstError = error;
}

void FileSearcher::search() {
    std::error_code root_ec;
    if (!fs::is_directory(rootPath, root_ec)) {
        throw std::runtime_error("cannot scan '" + rootPath.string() + "': not a directory");
    }

    {
        std::lock_guard<std::mutex> lock(results_mutex);
        matches.clear();
    }
    filesScanned.store(0);
    directoriesScanned.store(0);
    skippedPaths.store(0);
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        firstError = nullptr;
    }

    submit(rootPath);
    // Every directory task spawns further tasks for its subdirectories, so the
    // pool is only idle once the whole tree has been visited.
    pool.waitIdle();

    if (firstError) std::rethrow_exception(firstError);
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

SearchStats FileSearcher::stats() const {
    SearchStats result;
    result.files_scanned = filesScanned.load();
    result.directories_scanned = directoriesScanned.load();
    result.paths_skipped = skippedPaths.load();
    return result;
}

std::uint64_t FileSearcher::getScannedCount() const {
    return filesScanned.load();
}

std::uint64_t FileSearcher::getSkippedCount() const {
    return skippedPaths.load();
}
