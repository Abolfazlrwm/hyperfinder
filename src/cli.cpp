#include <hyperfinder/cli.h>

#include <hyperfinder/match.h>

#include <CLI/CLI.hpp>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// Provided by CMake from the project version. The fallback keeps the file
// compilable on its own (an editor's clangd, for instance).
#ifndef HYPERFINDER_VERSION
#define HYPERFINDER_VERSION "0.0.0"
#endif

namespace hyperfinder {
namespace {

constexpr const char* kDescription =
    "Multi-threaded file finder. Walks a directory tree and reports every file "
    "whose name matches the given pattern (a substring by default; --glob or "
    "--regex for other kinds).";

constexpr const char* kFooter =
    "Output: matching paths are written to stdout, one per line, and nothing "
    "else. The summary, warnings and errors go to stderr, so the results can be "
    "piped or redirected without filtering.\n"
    "Exit codes: 0 = at least one match, 1 = no match, 2 = error.";

// Upper bound on -j. ThreadPool would happily start 100000 threads, which
// never finishes and looks like a hang; reject it as a usage error instead.
constexpr std::size_t kMaxThreads = 1024;

ParseResult usageError(std::string message) {
    ParseResult result;
    result.exit_code = 2;
    result.stderr_text = std::move(message);
    result.stderr_text += "\nTry 'hyperfinder --help' for more information.\n";
    return result;
}

// Parses --color=auto|always|never into a ColorMode. Returns an empty string
// on success, an error message otherwise.
std::string parseColorMode(const std::string& value, ColorMode& out) {
    if (value == "auto") {
        out = ColorMode::Auto;
    } else if (value == "always") {
        out = ColorMode::Always;
    } else if (value == "never") {
        out = ColorMode::Never;
    } else {
        return "--color must be auto, always, or never (got '" + value + "')";
    }
    return {};
}

}  // namespace

ParseResult parseArguments(const std::vector<std::string>& args) {
    CLI::App app{kDescription, "hyperfinder"};
    app.footer(kFooter);
    app.set_version_flag("--version", std::string("hyperfinder ") + HYPERFINDER_VERSION);

    std::string query;
    std::string root = ".";
    std::size_t threads = 0;
    bool ignore_case_flag = false;
    bool case_sensitive_flag = false;
    bool glob_flag = false;
    bool regex_flag = false;
    std::size_t max_depth = kUnlimitedDepth;
    std::vector<std::string> exclude;
    std::vector<std::string> extensions;
    bool include_hidden = false;
    bool follow_symlinks = false;
    bool count_only = false;
    bool no_color = false;
    std::string color_str = "auto";

    app.add_option("query", query, "Substring to look for in file names")->required();
    app.add_option("path", root, "Directory to search")->capture_default_str();

    auto* threads_option = app.add_option(
        "-j,--threads", threads, "Worker threads; 0 means one per hardware thread");
    threads_option->capture_default_str();

    // -i is the default, so it exists only so that a script can state its
    // intent. The two flags contradict each other and CLI11 rejects the pair.
    auto* ignore_case_option =
        app.add_flag("-i,--ignore-case", ignore_case_flag, "Match case-insensitively (default)");
    auto* case_sensitive_option =
        app.add_flag("--case-sensitive", case_sensitive_flag, "Match case-sensitively");
    ignore_case_option->excludes(case_sensitive_option);
    case_sensitive_option->excludes(ignore_case_option);

    // The query can be a substring (default), a glob, or a regex, but never two
    // at once; the two flags reject each other.
    auto* glob_option =
        app.add_flag("--glob", glob_flag,
                     "Treat the query as a glob (* and ? wildcards), not a substring");
    auto* regex_option =
        app.add_flag("--regex", regex_flag,
                     "Treat the query as a regular expression (matched against the file name)");
    glob_option->excludes(regex_option);
    regex_option->excludes(glob_option);

    app.add_option(
        "--max-depth", max_depth,
        "Levels of directories to descend; 0 = the entries directly inside the "
        "root only (default: unlimited)");

    // expected(1) makes the option take exactly one value per occurrence,
    // instead of greedily swallowing the query and the path that follow it.
    // allow_extra_args(false) is required on top: CLI11 turns it on by default
    // for vector options and it overrides the expected count, which is how
    // "--ext log ." used to eat the positional too. TakeAll then keeps every
    // occurrence instead of rejecting the second one.
    app.add_option("--exclude", exclude,
                   "Glob pattern to exclude (matched against entry name); repeatable")
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);

    app.add_option("--ext", extensions,
                   "File extension to keep (with or without dot); repeatable; empty = any")
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);

    app.add_flag("--hidden", include_hidden,
                 "Descend into hidden directories and report hidden files");

    app.add_flag("--follow-symlinks", follow_symlinks,
                 "Follow directory symlinks (cycles are detected and skipped)");

    app.add_flag("--count", count_only,
                 "Print only the number of matches to stdout");

    auto* no_color_option = app.add_flag("--no-color", no_color,
                                         "Disable colour output (same as --color=never)");
    auto* color_option =
        app.add_option("--color", color_str,
                       "When to colour output: auto, always, or never")
        ->capture_default_str();
    no_color_option->excludes(color_option);

    // CLI11 wants argv-style input with the program name at index 0, which is
    // also what keeps the usage line and error messages correct.
    std::vector<std::string> owned = args;
    if (owned.empty()) owned.emplace_back("hyperfinder");
    std::vector<const char*> argv;
    argv.reserve(owned.size());
    for (const std::string& arg : owned) argv.push_back(arg.c_str());

    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
    } catch (const CLI::CallForAllHelp&) {
        ParseResult result;
        result.stdout_text = app.help();
        return result;
    } catch (const CLI::CallForHelp&) {
        ParseResult result;
        result.stdout_text = app.help();
        return result;
    } catch (const CLI::CallForVersion& version) {
        ParseResult result;
        result.stdout_text = std::string(version.what()) + "\n";
        return result;
    } catch (const CLI::ParseError& error) {
        return usageError(error.what());
    }

    if (threads > kMaxThreads) {
        return usageError("-j/--threads must be between 0 and " + std::to_string(kMaxThreads) +
                          " (got " + std::to_string(threads) + ")");
    }

    // Resolve the colour mode. --no-color wins over --color because it is the
    // explicit "off" switch; the two are also mutually exclusive via CLI11.
    ColorMode color_mode = ColorMode::Auto;
    if (no_color) {
        color_mode = ColorMode::Never;
    } else {
        const std::string error = parseColorMode(color_str, color_mode);
        if (!error.empty()) return usageError(error);
    }

    ParseResult result;
    result.should_run = true;
    result.options.query = std::move(query);
    result.options.root = std::move(root);
    result.options.threads = threads;
    result.options.ignore_case = !case_sensitive_flag;
    result.options.max_depth = max_depth;
    if (regex_flag) {
        result.options.match_mode = hyperfinder::MatchMode::Regex;
    } else if (glob_flag) {
        result.options.match_mode = hyperfinder::MatchMode::Glob;
    }
    result.options.exclude = std::move(exclude);

    // Normalise once here so the hot path only compares ready-made strings, and
    // drop empties: "--ext ''" would otherwise silently match nothing.
    for (const std::string& extension : extensions) {
        std::string normalised = normalizeExtension(extension);
        if (!normalised.empty()) result.options.extensions.push_back(std::move(normalised));
    }

    result.options.include_hidden = include_hidden;
    result.options.follow_symlinks = follow_symlinks;
    result.options.count_only = count_only;
    result.options.color = color_mode;
    return result;
}

}  // namespace hyperfinder
