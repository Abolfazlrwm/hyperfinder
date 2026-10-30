#include "output.h"

#include <hyperfinder/match.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hyperfinder {
namespace {

constexpr const char* kHighlight = "\x1b[1;32m";  // bold green
constexpr const char* kReset = "\x1b[0m";

bool stdoutIsTerminal() {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(::fileno(stdout)) != 0;
#endif
}

// Windows consoles only interpret ANSI escapes once virtual terminal
// processing is enabled. Old consoles refuse the flag; in that case the caller
// falls back to plain text rather than emitting raw escape sequences.
bool enableVirtualTerminal() {
#if defined(_WIN32)
    const HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return false;

    DWORD mode = 0;
    if (::GetConsoleMode(handle, &mode) == 0) return false;
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;
    return ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

bool noColorRequested() {
    // The NO_COLOR convention: any non-empty value disables colour.
    const char* value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
}

// Highlights the matched substring inside the file name, leaving the directory
// part and the rest of the name untouched.
std::string highlight(const std::string& path, const Options& options) {
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t name_start = separator == std::string::npos ? 0 : separator + 1;
    const std::string_view name(path.data() + name_start, path.size() - name_start);

    // Folding is ASCII-only, so it preserves byte length and the offsets found
    // in the folded copy are valid in the original.
    const std::string haystack = options.ignore_case ? toLowerAscii(name) : std::string(name);
    const std::string needle = options.ignore_case ? toLowerAscii(options.query) : options.query;
    if (needle.empty()) return path;

    const std::size_t position = haystack.find(needle);
    if (position == std::string::npos) return path;

    std::string out;
    out.reserve(path.size() + 16);
    out.append(path, 0, name_start + position);
    out += kHighlight;
    out.append(path, name_start + position, needle.size());
    out += kReset;
    out.append(path, name_start + position + needle.size(), std::string::npos);
    return out;
}

}  // namespace

void prepareConsole() {
#if defined(_WIN32)
    if (stdoutIsTerminal()) {
        // Windows Terminal and recent consoles render UTF-8 correctly once the
        // output code page says so. Without it, Persian names become mojibake.
        ::SetConsoleOutputCP(CP_UTF8);
    }
#endif
}

bool resolveColor(ColorMode mode) {
    if (mode == ColorMode::Never) return false;
    if (mode == ColorMode::Always) return enableVirtualTerminal();
    if (noColorRequested()) return false;
    if (!stdoutIsTerminal()) return false;
    return enableVirtualTerminal();
}

void writeMatches(const std::vector<std::string>& matches, const Options& options, bool color) {
    std::string buffer;
    buffer.reserve(matches.size() * 48);
    for (const std::string& path : matches) {
        if (color) {
            buffer += highlight(path, options);
        } else {
            buffer += path;
        }
        buffer += '\n';
    }
    std::cout.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::cout.flush();
}

void writeSummary(const SearchStats& stats, std::size_t match_count) {
    // stdout may be redirected into a pipe; flushing first keeps the summary
    // from appearing before the results when both go to a terminal.
    std::cout.flush();

    const auto plural = [](std::uint64_t count, const char* singular, const char* plural_form) {
        return std::to_string(count) + " " + (count == 1 ? singular : plural_form);
    };

    if (stats.paths_skipped > 0) {
        std::cerr << "warning: " << stats.paths_skipped
                  << " path(s) could not be read and were skipped\n";
    }
    std::cerr << plural(static_cast<std::uint64_t>(match_count), "match", "matches") << ", "
              << plural(stats.files_scanned, "file", "files") << ", "
              << plural(stats.directories_scanned, "directory", "directories")
              << " scanned in " << stats.elapsed_seconds << " s\n";
}

}  // namespace hyperfinder
