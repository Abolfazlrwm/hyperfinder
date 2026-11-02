// Ablation micro-benchmark: how much does each per-entry step cost?
//
// This is instrumentation, not a product tool. It walks the same tree with the
// same primitives the searcher uses and adds one step at a time, so the profile
// says which step is worth optimising instead of which one is easy to blame.
//
//   iter        std::filesystem::recursive_directory_iterator, count only
//   iter-status the same, plus directory_entry::symlink_status() per entry
//   iter-name   the same, plus filename().u8string() per entry (UTF-8 name)
//   iter-attr   the same, plus GetFileAttributesW per entry (what the searcher
//               does today on Windows to see reparse points and hidden files)
//   findex      FindFirstFileExW + FIND_FIRST_EX_LARGE_FETCH, count only
//
// The last one is the candidate replacement: it returns attributes together
// with the names, so the extra per-entry call disappears.
//
// Usage: walk_bench <mode> <root> [runs]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

volatile std::size_t g_sink = 0;

using Clock = std::chrono::steady_clock;

double medianMs(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    if (samples.size() % 2 == 1) return samples[middle];
    return (samples[middle - 1] + samples[middle]) / 2.0;
}

template <typename PerEntry>
void walkWithIterator(const fs::path& root, PerEntry per_entry) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    const fs::recursive_directory_iterator end;
    std::size_t count = 0;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& entry = *it;
        per_entry(entry);
        ++count;
    }
    g_sink += count;
}

#if defined(_WIN32)
void walkWithFindEx(const fs::path& root) {
    // Iterative depth-first walk over an explicit stack: recursion would be
    // simpler but this keeps the stack small on a deep tree.
    std::vector<std::wstring> pending;
    pending.push_back(root.wstring());
    std::size_t count = 0;

    while (!pending.empty()) {
        std::wstring directory = std::move(pending.back());
        pending.pop_back();

        std::wstring pattern = directory;
        if (!pattern.empty() && pattern.back() != L'\\') pattern.push_back(L'\\');
        pattern.push_back(L'*');

        WIN32_FIND_DATAW data;
        // FIND_FIRST_EX_LARGE_FETCH asks the provider for a bigger buffer, which
        // cuts the number of round trips for a directory with many entries.
        HANDLE handle = ::FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                           FindExSearchNameMatch, nullptr,
                                           FIND_FIRST_EX_LARGE_FETCH);
        if (handle == INVALID_HANDLE_VALUE) continue;

        do {
            const wchar_t* name = data.cFileName;
            if (name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'))) {
                continue;
            }
            ++count;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                std::wstring child = directory;
                if (!child.empty() && child.back() != L'\\') child.push_back(L'\\');
                child.append(name);
                pending.push_back(std::move(child));
            }
        } while (::FindNextFileW(handle, &data));

        ::FindClose(handle);
    }
    g_sink += count;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: walk_bench <mode> <root> [runs]\n");
        return 2;
    }
    const std::string mode = argv[1];
    const fs::path root = fs::u8path(argv[2]);
    const int runs = argc > 3 ? std::atoi(argv[3]) : 5;

    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(runs));
    std::size_t last_count = 0;

    for (int run = 0; run < runs; ++run) {
        const std::size_t sink_before = g_sink;
        const auto started = Clock::now();

        if (mode == "iter") {
            walkWithIterator(root, [](const fs::directory_entry&) {});
        } else if (mode == "iter-status") {
            walkWithIterator(root, [](const fs::directory_entry& entry) {
                std::error_code ec;
                g_sink += static_cast<std::size_t>(entry.symlink_status(ec).type());
            });
        } else if (mode == "iter-name") {
            walkWithIterator(root, [](const fs::directory_entry& entry) {
                g_sink += entry.path().filename().u8string().size();
            });
        } else if (mode == "iter-attr") {
            walkWithIterator(root, [](const fs::directory_entry& entry) {
                std::error_code ec;
                g_sink += static_cast<std::size_t>(entry.symlink_status(ec).type());
#if defined(_WIN32)
                g_sink += ::GetFileAttributesW(entry.path().c_str());
#endif
            });
        } else if (mode == "findex") {
#if defined(_WIN32)
            walkWithFindEx(root);
#else
            std::fprintf(stderr, "findex is Windows only\n");
            return 2;
#endif
        } else {
            std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
            return 2;
        }

        last_count = g_sink - sink_before;
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
    }

    const double median = medianMs(samples);
    // The entry count is printed on purpose: a mode that is fast because it
    // walked less is not a faster mode.
    std::printf("%-12s median %8.1f ms  min %8.1f ms  max %8.1f ms  (n=%d, %zu entries)\n",
                mode.c_str(), median, *std::min_element(samples.begin(), samples.end()),
                *std::max_element(samples.begin(), samples.end()), runs, last_count);
    return 0;
}
