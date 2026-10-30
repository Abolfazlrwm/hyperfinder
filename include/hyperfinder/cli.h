#ifndef HYPERFINDER_CLI_H
#define HYPERFINDER_CLI_H

#include <string>
#include <vector>

#include <hyperfinder/options.h>

namespace hyperfinder {

// Outcome of parsing a command line.
//
// Parsing does not print and does not exit, so the whole command line can be
// tested without spawning a process. main() is left with printing the two
// text buffers and returning the exit code.
struct ParseResult {
    // True when there is a search to run. False for --help, --version and any
    // parse error, in which case exit_code is already final.
    bool should_run = false;

    // 0 for --help/--version, 2 for a usage error.
    int exit_code = 0;

    // Only meaningful when should_run is true.
    Options options;

    // Text for stdout (help, version) and stderr (error message), respectively.
    // Exactly one of them is non-empty when should_run is false.
    std::string stdout_text;
    std::string stderr_text;
};

// Parses args, where args[0] is the program name. An empty vector is treated as
// a bare invocation of the program.
ParseResult parseArguments(const std::vector<std::string>& args);

}  // namespace hyperfinder

#endif  // HYPERFINDER_CLI_H
