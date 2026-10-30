#ifndef HYPERFINDER_OPTIONS_H
#define HYPERFINDER_OPTIONS_H

#include <cstddef>
#include <string>
#include <vector>

namespace hyperfinder {

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

    // Directory to search, as UTF-8. The root itself is at depth 0.
    std::string root = ".";

    // 0 means "one worker per hardware thread".
    std::size_t threads = 0;

    // ASCII-only case folding: see the note in match.h.
    bool ignore_case = true;

    // 0 means unlimited. Entries directly inside the root are at depth 1.
    std::size_t max_depth = 0;

    // Glob patterns matched against the entry name. A matching directory is not
    // descended into; a matching file is not reported.
    std::vector<std::string> exclude;

    // File extensions, with or without a leading dot. Empty means "any".
    std::vector<std::string> extensions;

    // A name is hidden when it starts with a dot.
    bool include_hidden = false;

    bool follow_symlinks = false;

    // Print only the number of matches.
    bool count_only = false;

    ColorMode color = ColorMode::Auto;
};

}  // namespace hyperfinder

#endif  // HYPERFINDER_OPTIONS_H
