#ifndef HYPERFINDER_OPTIONS_H
#define HYPERFINDER_OPTIONS_H

#include <cstddef>
#include <string>
#include <vector>

namespace hyperfinder {

// Value for Options::max_depth meaning "no limit". Chosen over 0 because 0 is
// a meaningful depth: it means "only the entries directly inside the root".
constexpr std::size_t kUnlimitedDepth = static_cast<std::size_t>(-1);

// When to emit ANSI colour escapes.
enum class ColorMode {
    Auto,    // only when stdout is a terminal, and not disabled by NO_COLOR
    Always,  // even when stdout is a pipe
    Never,
};

// Everything the command line can change, in the form the core library needs.
//
// Deliberately a plain aggregate with no parsing, no I/O and no OS calls: the
// command-line layer fills it in, tests can build one by hand, and the search
// code never has to know that a command line exists.
struct Options {
    // Substring to look for in file names.
    std::string query;

    // Directory to search, as UTF-8.
    std::string root = ".";

    // 0 means "one worker per hardware thread".
    std::size_t threads = 0;

    // ASCII-only case folding: see the note in match.h.
    bool ignore_case = true;

    // How many directory levels to descend. The root itself is level 0, so 0
    // reports the entries directly inside the root and descends no further,
    // 1 also descends into each first-level subdirectory, and so on.
    // kUnlimitedDepth (the default) means no limit.
    std::size_t max_depth = kUnlimitedDepth;

    // Glob patterns matched against the entry name. A matching directory is not
    // descended into; a matching file is not reported.
    std::vector<std::string> exclude;

    // File extensions, normalised to a leading dot and lower case. Empty means
    // "any". Only the last extension of a name is considered.
    std::vector<std::string> extensions;

    // Hidden entries are skipped unless this is set.
    bool include_hidden = false;

    // Follow directory links. Off by default, because a cycle would otherwise
    // make the scan run forever.
    bool follow_symlinks = false;

    // Print only the number of matches.
    bool count_only = false;

    ColorMode color = ColorMode::Auto;
};

}  // namespace hyperfinder

#endif  // HYPERFINDER_OPTIONS_H
