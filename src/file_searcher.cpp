#include <hyperfinder/file_searcher.h>

#include <hyperfinder/match.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

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

namespace fs = std::filesystem;

#if defined(_WIN32)
std::string wideToUtf8(const wchar_t* text, std::size_t length) {
    if (length == 0) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr,
                                             0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), needed, nullptr,
                          nullptr);
    return out;
}
#endif

// A key that is identical for two paths that name the same directory.
//
// std::filesystem is not enough on Windows: libstdc++'s weakly_canonical only
// resolves symbolic links, and a junction is invisible to it, so a junction
// pointing back at an ancestor produced a fresh key at every level and the scan
// recursed until MAX_PATH. Opening the directory and asking Win32 for its final
// path resolves junctions too.
std::string canonicalKey(const fs::path& directory) {
#if defined(_WIN32)
    const HANDLE handle =
        ::CreateFileW(directory.c_str(), 0,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        std::wstring buffer(32768, L'\0');
        const DWORD length =
            ::GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        ::CloseHandle(handle);
        if (length > 0 && length < buffer.size()) {
            // NTFS compares names case-insensitively, so fold before comparing.
            return hyperfinder::toLowerAscii(wideToUtf8(buffer.data(), length));
        }
    }
#endif
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(directory, ec);
    return (ec ? directory : canonical).u8string();
}

// What the scan needs to know about one entry before deciding what to do.
struct EntryFlags {
    bool hidden = false;
    bool link = false;
};

// On Windows a single GetFileAttributesW answers both questions. It has to be
// Win32: libstdc++'s symlink_status() resolves the target, so a junction looks
// like a plain directory and std::filesystem cannot see the reparse point at
// all. On POSIX symlink_status() already reports links, and the dot convention
// covers hidden names.
EntryFlags inspectEntry(const fs::directory_entry& entry, const fs::file_status& link_status,
                        std::string_view name) {
    EntryFlags flags;
    flags.hidden = hyperfinder::isHiddenByName(name);
    flags.link = fs::is_symlink(link_status);
#if defined(_WIN32)
    const DWORD attributes = ::GetFileAttributesW(entry.path().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        flags.hidden = flags.hidden || (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        flags.link = flags.link || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }
#endif
    return flags;
}

// --- Time helpers for --newer-than -----------------------------------------
//
// C++17 has no std::chrono::clock_cast, so a system_clock instant cannot be
// converted to a file_clock instant directly. Both clocks are wall clocks that
// advance at the same rate, so sampling their current offset once and applying
// it is enough for a date filter.
//
// Everything is done in whole seconds on purpose. std::filesystem::file_time_type
// is nanosecond-based on libstdc++, and int64 nanoseconds overflow in 2262, so a
// cutoff like 2999-01-01 cannot be represented as a file_time_type at all
// (system_clock::from_time_t would overflow while scaling seconds to
// nanoseconds). Seconds are plenty for a one-day-granularity filter.

// Offset between the file clock and the system clock, in whole seconds.
std::int64_t fileClockOffsetSeconds() {
    const auto sys_now = std::chrono::system_clock::now();
    const auto file_now = fs::file_time_type::clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(file_now.time_since_epoch()).count() -
           std::chrono::duration_cast<std::chrono::seconds>(sys_now.time_since_epoch()).count();
}

// Converts a system-clock time_t (seconds since the Unix epoch) to seconds on
// the file clock.
std::int64_t systemSecondsToFileSeconds(std::time_t system_seconds) {
    return static_cast<std::int64_t>(system_seconds) + fileClockOffsetSeconds();
}

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
    static const int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    return lengths[month - 1];
}

// Parses "YYYY-MM-DD" as local midnight and returns it in whole seconds on the
// file clock. Returns false when the text is not a valid date; the day is
// checked against the real length of the month, so "2026-02-31" is rejected
// rather than silently rolled into March.
bool parseIsoDate(std::string_view text, std::int64_t& out_seconds) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;

    const auto digits = [](std::string_view s, std::size_t offset, std::size_t count, int& value) {
        value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const char c = s[offset + i];
            if (c < '0' || c > '9') return false;
            value = value * 10 + (c - '0');
        }
        return true;
    };

    int year = 0;
    int month = 0;
    int day = 0;
    if (!digits(text, 0, 4, year) || !digits(text, 5, 2, month) || !digits(text, 8, 2, day)) {
        return false;
    }
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > daysInMonth(year, month)) return false;

    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;  // let mktime decide whether DST applies to that date
    const std::time_t local = std::mktime(&tm);
    if (local == static_cast<std::time_t>(-1)) return false;

    out_seconds = systemSecondsToFileSeconds(local);
    return true;
}

// --- Content search helpers -------------------------------------------------

// Streaming read size. A match that straddles two chunks is still found because
// the current line is accumulated across chunk boundaries before it is tested.
constexpr std::size_t kContentChunk = 64 * 1024;
// A file whose first bytes contain a NUL is treated as binary and skipped.
constexpr std::size_t kBinaryProbe = 8 * 1024;
// Long lines are cut to this many bytes before they are printed.
constexpr std::size_t kMaxLineLength = 200;

// ASCII-only case-insensitive find. `needle_lower` must already be lower-cased;
// bytes >= 0x80 are compared unchanged, so UTF-8 sequences match exactly.
std::size_t findAsciiCaseInsensitive(std::string_view haystack, std::string_view needle_lower) {
    if (needle_lower.empty()) return 0;
    if (needle_lower.size() > haystack.size()) return std::string_view::npos;
    const std::size_t n = needle_lower.size();
    for (std::size_t i = 0; i + n <= haystack.size(); ++i) {
        std::size_t j = 0;
        for (; j < n; ++j) {
            char c = haystack[i + j];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != needle_lower[j]) break;
        }
        if (j == n) return i;
    }
    return std::string_view::npos;
}

// Truncates `line` to `max` bytes without splitting a UTF-8 code point, and
// marks the cut with an ellipsis.
std::string truncateLine(std::string_view line, std::size_t max) {
    if (line.size() <= max) return std::string(line);
    std::size_t cut = max;
    // Back off while the byte at the cut is a continuation byte (10xxxxxx), so
    // the ellipsis never lands inside a multi-byte sequence.
    while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) --cut;
    std::string out(line.substr(0, cut));
    out += "...";
    return out;
}

}  // namespace

namespace hyperfinder {

FileSearcher::FileSearcher(Options opts)
    : options(std::move(opts)),
      query(options.ignore_case ? toLowerAscii(options.query) : options.query),
      match_mode_(options.match_mode),
      // std::regex with ECMAScript grammar and, when case-insensitive, the icase
      // flag (which folds ASCII only, matching the project's case rule). A bad
      // pattern throws std::regex_error here; the caller reports it as a usage
      // error with exit code 2.
      regex_(match_mode_ == MatchMode::Regex
                 ? std::regex(options.query, options.ignore_case
                                                       ? std::regex::ECMAScript | std::regex::icase
                                                       : std::regex::ECMAScript)
                 : std::regex()),
      rootPath(fs::u8path(options.root)),
      content_needle_(options.ignore_case ? toLowerAscii(options.content) : options.content),
      pool(options.threads == 0 ? ThreadPool::defaultThreadCount() : options.threads) {
    // Resolve the --newer-than cutoff once, before any worker starts. A relative
    // window is measured back from "now" on the file clock; an ISO date is local
    // midnight of that day, converted from the system clock. The CLI validates
    // the date, so a bad value here means the two layers disagree; throw rather
    // than silently matching everything.
    if (options.newer_than_enabled) {
        if (!options.newer_than_date.empty()) {
            if (!parseIsoDate(options.newer_than_date, cutoff_seconds_)) {
                throw std::invalid_argument("invalid --newer-than date: " +
                                            options.newer_than_date);
            }
        } else {
            cutoff_seconds_ =
                std::chrono::duration_cast<std::chrono::seconds>(
                    fs::file_time_type::clock::now().time_since_epoch()).count() -
                options.newer_than.count();
        }
        has_cutoff_ = true;
    }

    // Normalise the separator style once, at the root. std::filesystem appends
    // the native separator to whatever form the parent path already has, so a
    // root written as "C:/data" produced results like "C:/data\sub\file.txt".
    rootPath.make_preferred();
}

FileSearcher::FileSearcher(const std::string& q, const fs::path& root)
    : FileSearcher([&q, &root] {
          Options opts;
          opts.query = q;
          opts.root = root.u8string();
          return opts;
      }()) {}

bool FileSearcher::nameMatches(const std::string& filename) const {
    switch (match_mode_) {
        case MatchMode::Substring:
            return options.ignore_case ? toLowerAscii(filename).find(query) != std::string::npos
                                       : filename.find(query) != std::string::npos;

        case MatchMode::Glob: {
            // Fold both sides when case-insensitive; globMatch is byte-wise, so the
            // ASCII-only fold keeps multi-byte names intact.
            const std::string needle =
                options.ignore_case ? toLowerAscii(options.query) : options.query;
            const std::string haystack =
                options.ignore_case ? toLowerAscii(filename) : filename;
            return globMatch(needle, haystack);
        }

        case MatchMode::Regex:
            // regex_search, not full match: a query like "log" matches "build.log".
            return std::regex_search(filename, regex_);
    }
    return false;  // unreachable
}

bool FileSearcher::passesSizeAndTime(const fs::path& path) {
    const bool size_filter = options.min_size > 0 ||
                             options.max_size != std::numeric_limits<std::uintmax_t>::max();
    if (size_filter) {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);
        if (ec) {
            skippedPaths.fetch_add(1);
            return false;
        }
        if (size < options.min_size || size > options.max_size) return false;
    }

    if (has_cutoff_) {
        std::error_code ec;
        const fs::file_time_type mtime = fs::last_write_time(path, ec);
        if (ec) {
            skippedPaths.fetch_add(1);
            return false;
        }
        const std::int64_t mtime_seconds =
            std::chrono::duration_cast<std::chrono::seconds>(mtime.time_since_epoch()).count();
        // "Newer than X" includes X itself, so a file touched exactly at the
        // cutoff is reported.
        if (mtime_seconds < cutoff_seconds_) return false;
    }

    return true;
}

void FileSearcher::submitContent(const fs::path& path) {
    pool.enqueue([this, path] {
        try {
            scanContent(path);
        } catch (...) {
            recordError(std::current_exception());
        }
    });
}

void FileSearcher::scanContent(const fs::path& path) {
    // Size guard: a file bigger than the limit is skipped rather than read.
    std::error_code size_ec;
    const std::uintmax_t size = fs::file_size(path, size_ec);
    if (size_ec) {
        skippedPaths.fetch_add(1);
        return;
    }
    if (size > options.content_max_size) return;

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        skippedPaths.fetch_add(1);
        return;
    }

    std::vector<ContentMatch> local;
    std::string line;
    line.reserve(256);
    std::size_t line_number = 1;
    bool binary = false;
    bool first_chunk = true;

    const auto process_line = [&]() {
        std::string_view view(line);
        // CRLF: the CR belongs to the line terminator, not the text.
        if (!view.empty() && view.back() == '\r') view.remove_suffix(1);

        const bool hit = options.ignore_case
                             ? findAsciiCaseInsensitive(view, content_needle_) !=
                                   std::string_view::npos
                             : view.find(options.content) != std::string_view::npos;
        if (hit) {
            ContentMatch match;
            match.path = path.u8string();
            match.line = line_number;
            match.text = truncateLine(view, kMaxLineLength);
            local.push_back(std::move(match));
        }
        line.clear();
    };

    // Feeds a chunk into the current-line buffer. The line is only tested at a
    // '\n', so a match split across two chunks is still found: both halves are
    // appended to the same buffer before it is examined.
    const auto consume = [&](const char* data, std::size_t count) {
        if (first_chunk) {
            first_chunk = false;
            const std::size_t probe = count < kBinaryProbe ? count : kBinaryProbe;
            if (std::memchr(data, '\0', probe) != nullptr) {
                binary = true;
                return;
            }
        }
        const char* p = data;
        const char* const end = data + count;
        while (p < end) {
            const auto* nl = static_cast<const char*>(
                std::memchr(p, '\n', static_cast<std::size_t>(end - p)));
            if (nl == nullptr) {
                line.append(p, static_cast<std::size_t>(end - p));
                break;
            }
            line.append(p, static_cast<std::size_t>(nl - p));
            process_line();
            ++line_number;
            p = nl + 1;
        }
    };

    std::vector<char> buffer(kContentChunk);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        consume(buffer.data(), static_cast<std::size_t>(got));
        if (binary) break;
    }
    if (binary) return;

    // A file that does not end in a newline still has a final line.
    if (!line.empty()) process_line();

    if (!local.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        matches.push_back(path.u8string());
        content_matches_.insert(content_matches_.end(), std::make_move_iterator(local.begin()),
                                std::make_move_iterator(local.end()));
    }
}

void FileSearcher::scanDirectory(const fs::path& path, std::size_t depth) {
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

    // Matches found by this task, merged under one lock when the directory is
    // finished, so a directory with many hits takes the results mutex once
    // instead of once per match.
    std::vector<std::string> found;

    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry& entry = *it;

        std::error_code entry_ec;
        const fs::file_status link_status = entry.symlink_status(entry_ec);
        if (entry_ec) {
            skippedPaths.fetch_add(1);
        } else {
            const std::string name = entry.path().filename().u8string();
            const EntryFlags flags = inspectEntry(entry, link_status, name);

            // Hidden and excluded entries are pruned before anything else: not
            // counted, not reported, and a directory among them is never
            // enqueued as a task.
            const bool pruned = (!options.include_hidden && flags.hidden) ||
                                matchesAnyGlob(name, options.exclude);

            if (!pruned) {
                // What the entry really is. Without --follow-symlinks a link is
                // neither a directory nor a regular file here, which is what
                // keeps the "never follow a link" guarantee.
                fs::file_status effective = link_status;
                bool resolved = true;
                if (flags.link && options.follow_symlinks) {
                    std::error_code target_ec;
                    effective = entry.status(target_ec);
                    if (target_ec) {
                        skippedPaths.fetch_add(1);
                        resolved = false;
                    }
                }

                if (resolved && fs::is_directory(effective)) {
                    // `depth` is the level of this directory, so its children
                    // sit at depth + 1 and may only be descended into while
                    // depth < max_depth.
                    if (depth < options.max_depth) {
                        bool descend = true;
                        if (options.follow_symlinks) {
                            // Every directory reached is recorded, so a link or
                            // junction pointing back at an ancestor is caught
                            // before it can be scanned twice.
                            descend = !markVisited(entry.path());
                        } else {
                            descend = !flags.link;
                        }
                        if (descend) submit(entry.path(), depth + 1);
                    }
                } else if (resolved && fs::is_regular_file(effective)) {
                    filesScanned.fetch_add(1);
                    // An empty query means "any name": --content is usually run
                    // without a name filter.
                    const bool name_ok = options.query.empty() || nameMatches(name);
                    if (matchesExtension(name, options.extensions) && name_ok &&
                        passesSizeAndTime(entry.path())) {
                        if (options.content.empty()) {
                            found.push_back(entry.path().u8string());
                        } else {
                            submitContent(entry.path());
                        }
                    }
                }
            }
        }

        it.increment(ec);
        if (ec) {
            skippedPaths.fetch_add(1);
            break;
        }
    }

    if (!found.empty()) {
        std::lock_guard<std::mutex> lock(results_mutex);
        matches.insert(matches.end(), std::make_move_iterator(found.begin()),
                       std::make_move_iterator(found.end()));
    }
}

void FileSearcher::submit(const fs::path& path, std::size_t depth) {
    pool.enqueue([this, path, depth] {
        try {
            scanDirectory(path, depth);
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

bool FileSearcher::markVisited(const fs::path& directory) {
    const std::string key = canonicalKey(directory);
    std::lock_guard<std::mutex> lock(visited_mutex);
    return !visited_directories.insert(key).second;
}

void FileSearcher::search() {
    std::error_code root_ec;
    if (!fs::is_directory(rootPath, root_ec)) {
        throw std::runtime_error("cannot scan '" + rootPath.u8string() + "': not a directory");
    }

    {
        std::lock_guard<std::mutex> lock(results_mutex);
        matches.clear();
        content_matches_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(visited_mutex);
        visited_directories.clear();
    }
    filesScanned.store(0);
    directoriesScanned.store(0);
    skippedPaths.store(0);
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        firstError = nullptr;
    }

    const auto start = std::chrono::steady_clock::now();

    // The root is marked so that a link pointing back at it is not followed a
    // second time.
    if (options.follow_symlinks) markVisited(rootPath);

    submit(rootPath, 0);
    // Every directory task spawns further tasks for its subdirectories, so the
    // pool is only idle once the whole tree has been visited.
    pool.waitIdle();

    if (firstError) std::rethrow_exception(firstError);

    // Worker threads finish in an unpredictable order, so the raw result order
    // changes from run to run. Sort once here; no task is running any more.
    std::sort(matches.begin(), matches.end());
    std::sort(content_matches_.begin(), content_matches_.end(),
              [](const ContentMatch& a, const ContentMatch& b) {
                  if (a.path != b.path) return a.path < b.path;
                  return a.line < b.line;
              });

    elapsedSeconds.store(std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
                             .count());
}

const std::vector<std::string>& FileSearcher::results() const noexcept {
    return matches;
}

const std::vector<ContentMatch>& FileSearcher::contentMatches() const noexcept {
    return content_matches_;
}

SearchStats FileSearcher::stats() const {
    SearchStats result;
    result.files_scanned = filesScanned.load();
    result.directories_scanned = directoriesScanned.load();
    result.paths_skipped = skippedPaths.load();
    result.elapsed_seconds = elapsedSeconds.load();
    return result;
}

std::uint64_t FileSearcher::getScannedCount() const {
    return filesScanned.load();
}

std::uint64_t FileSearcher::getSkippedCount() const {
    return skippedPaths.load();
}

}  // namespace hyperfinder
