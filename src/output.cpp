#include "output.h"

#include <hyperfinder/json.h>
#include <hyperfinder/match.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <fcntl.h>
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

// Human-friendly elapsed time: "16.9 ms" below a second, "1.24 s" above. Two
// significant decimals keep the number readable without pretending to a
// precision the clock does not have.
std::string formatDuration(double seconds) {
    char buffer[32];
    if (seconds < 1.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f ms", seconds * 1000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f s", seconds);
    }
    return buffer;
}

// Highlights the part of the file name that the query matched, leaving the
// directory part and the rest of the name untouched. What "matched" means
// depends on the match mode: a literal substring, the whole name for a glob
// (the pattern describes the entire name), or the span a regex search found.
// `pattern` is the compiled regex in regex mode and null otherwise.
std::string highlight(const std::string& path, const Options& options,
                      const std::regex* pattern) {
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t name_start = separator == std::string::npos ? 0 : separator + 1;
    const std::string name = path.substr(name_start);

    std::size_t offset = 0;
    std::size_t length = 0;
    switch (options.match_mode) {
        case MatchMode::Glob:
            length = name.size();
            break;

        case MatchMode::Regex: {
            if (pattern == nullptr) return path;
            std::smatch found;
            if (!std::regex_search(name, found, *pattern)) return path;
            offset = static_cast<std::size_t>(found.position());
            length = static_cast<std::size_t>(found.length());
            break;
        }

        case MatchMode::Substring:
        default: {
            // Folding is ASCII-only, so it preserves byte length and the offsets
            // found in the folded copy are valid in the original.
            const std::string haystack =
                options.ignore_case ? toLowerAscii(name) : name;
            const std::string needle =
                options.ignore_case ? toLowerAscii(options.query) : options.query;
            if (needle.empty()) return path;
            const std::size_t position = haystack.find(needle);
            if (position == std::string::npos) return path;
            offset = position;
            length = needle.size();
            break;
        }
    }
    // A pattern such as "a*" can match the empty string; there is nothing to
    // paint in that case, and an empty span would emit a stray escape pair.
    if (length == 0) return path;

    std::string out;
    out.reserve(path.size() + 16);
    out.append(path, 0, name_start + offset);
    out += kHighlight;
    out.append(path, name_start + offset, length);
    out += kReset;
    out.append(path, name_start + offset + length, std::string::npos);
    return out;
}

// Highlights the matched substring inside a content line. `text` is already
// truncated to a printable width, so the match may fall outside it; in that
// case the line is printed plain.
std::string highlightContent(const std::string& text, const Options& options) {
    const std::string haystack = options.ignore_case ? toLowerAscii(text) : text;
    const std::string needle = options.ignore_case ? toLowerAscii(options.content) : options.content;
    if (needle.empty()) return text;

    const std::size_t position = haystack.find(needle);
    if (position == std::string::npos) return text;

    std::string out;
    out.reserve(text.size() + 16);
    out.append(text, 0, position);
    out += kHighlight;
    out.append(text, position, needle.size());
    out += kReset;
    out.append(text, position + needle.size(), std::string::npos);
    return out;
}

}  // namespace

void prepareConsole() {
#if defined(_WIN32)
    if (stdoutIsTerminal()) {
        // Windows Terminal and recent consoles render UTF-8 correctly once the
        // output code page says so. Without it, Persian names become mojibake.
        ::SetConsoleOutputCP(CP_UTF8);
    } else {
        // Redirected or piped: write the bytes exactly as built. The CRT's text
        // mode would turn every '\n' into "\r\n", which is wrong for a tool
        // whose output is meant to be parsed. The console keeps text mode
        // because a bare '\n' there moves down without returning to column 0.
        ::_setmode(::_fileno(stdout), _O_BINARY);
    }
#endif
}

bool resolveColor(ColorMode mode) {
    if (mode == ColorMode::Never) return false;
    if (noColorRequested()) return false;
    if (mode == ColorMode::Always) {
        // --color=always forces colour onto a pipe as well; enabling virtual
        // terminal processing is best effort here, because on a real console it
        // lets the escapes render, but a pipe has no console to enable it on.
        enableVirtualTerminal();
        return true;
    }
    // Auto: only when stdout is a terminal.
    if (!stdoutIsTerminal()) return false;
    return enableVirtualTerminal();
}

void writeMatches(const std::vector<std::string>& matches, const Options& options, bool color) {
    // Compile the pattern once per run rather than once per match. The command
    // line layer has already rejected an invalid pattern, but this file is also
    // reachable from the library, so a failure here degrades to no colour
    // instead of throwing out of the writer.
    std::regex pattern;
    const std::regex* compiled = nullptr;
    if (color && options.match_mode == MatchMode::Regex) {
        try {
            pattern = std::regex(options.query, options.ignore_case
                                                   ? std::regex::ECMAScript | std::regex::icase
                                                   : std::regex::ECMAScript);
            compiled = &pattern;
        } catch (const std::regex_error&) {
            compiled = nullptr;
        }
    }

    std::string buffer;
    buffer.reserve(matches.size() * 48);
    for (const std::string& path : matches) {
        if (color) {
            buffer += highlight(path, options, compiled);
        } else {
            buffer += path;
        }
        buffer += '\n';
    }
    std::cout.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::cout.flush();
}

void writeContentMatches(const std::vector<ContentMatch>& matches, const Options& options,
                         bool color) {
    std::string buffer;
    buffer.reserve(matches.size() * 96);
    for (const ContentMatch& match : matches) {
        buffer += match.path;
        buffer += ':';
        buffer += std::to_string(match.line);
        buffer += ':';
        if (color) {
            buffer += highlightContent(match.text, options);
        } else {
            buffer += match.text;
        }
        buffer += '\n';
    }
    std::cout.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::cout.flush();
}

void writeJsonMatches(const std::vector<std::string>& paths,
                      const std::vector<ContentMatch>& content_matches, bool content_mode) {
    std::string buffer;
    if (content_mode) {
        buffer.reserve(content_matches.size() * 96);
        for (const ContentMatch& match : content_matches) {
            buffer += "{\"path\":\"";
            buffer += jsonEscape(match.path);
            buffer += "\",\"line\":";
            buffer += std::to_string(match.line);
            buffer += ",\"text\":\"";
            buffer += jsonEscape(match.text);
            buffer += "\"}\n";
        }
    } else {
        buffer.reserve(paths.size() * 48);
        for (const std::string& path : paths) {
            buffer += "{\"path\":\"";
            buffer += jsonEscape(path);
            buffer += "\"}\n";
        }
    }
    std::cout.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::cout.flush();
}

void writePrint0Matches(const std::vector<std::string>& paths,
                        const std::vector<ContentMatch>& content_matches, bool content_mode) {
    std::string buffer;
    if (content_mode) {
        buffer.reserve(content_matches.size() * 64);
        for (const ContentMatch& match : content_matches) {
            buffer += match.path;
            buffer += ':';
            buffer += std::to_string(match.line);
            buffer += ':';
            buffer += match.text;
            buffer += '\0';
        }
    } else {
        buffer.reserve(paths.size() * 48);
        for (const std::string& path : paths) {
            buffer += path;
            buffer += '\0';
        }
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
              << " scanned in " << formatDuration(stats.elapsed_seconds) << "\n";
}

}  // namespace hyperfinder
