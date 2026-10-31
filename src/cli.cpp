#include <hyperfinder/cli.h>

#include <hyperfinder/match.h>

#include <CLI/CLI.hpp>

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
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

// Parses a byte size such as "0", "512", "10k", "5M" or "2G". Units are
// decimal (1k = 1000), matching how disk sizes are usually quoted. An optional
// trailing "b" is accepted ("10kb"). Returns an empty string on success, an
// error message otherwise.
std::string parseSize(const std::string& text, std::uintmax_t& out) {
    if (text.empty()) return "--min-size/--max-size must not be empty";

    std::uintmax_t value = 0;
    std::size_t i = 0;
    bool any_digit = false;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        any_digit = true;
        const std::uintmax_t digit = static_cast<std::uintmax_t>(text[i] - '0');
        if (value > (std::numeric_limits<std::uintmax_t>::max() - digit) / 10) {
            return "size '" + text + "' is too large";
        }
        value = value * 10 + digit;
    }
    if (!any_digit) return "size '" + text + "' must start with a number";

    std::uintmax_t multiplier = 1;
    if (i < text.size()) {
        const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        if (suffix == 'k') {
            multiplier = 1000ull;
        } else if (suffix == 'm') {
            multiplier = 1000ull * 1000ull;
        } else if (suffix == 'g') {
            multiplier = 1000ull * 1000ull * 1000ull;
        } else {
            return "size '" + text + "' has an unknown suffix (use k, M or G)";
        }
        ++i;
        if (i < text.size() && (text[i] == 'b' || text[i] == 'B')) ++i;
    }
    if (i != text.size()) return "size '" + text + "' has trailing characters";

    if (value != 0 && value > std::numeric_limits<std::uintmax_t>::max() / multiplier) {
        return "size '" + text + "' is too large";
    }
    out = value * multiplier;
    return {};
}

// Parses --newer-than. Accepts a duration (digits plus one of s, m, h, d, w) or
// an ISO date (YYYY-MM-DD). The date is only range-checked here; the searcher
// does the exact calendar validation when it resolves the cutoff.
std::string parseNewerThan(const std::string& text, bool& enabled, std::chrono::seconds& window,
                           std::string& date) {
    if (text.size() == 10 && text[4] == '-' && text[7] == '-') {
        int year = 0;
        int month = 0;
        int day = 0;
        const auto digits = [](const std::string& s, std::size_t offset, std::size_t count,
                               int& value) {
            value = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const char c = s[offset + i];
                if (c < '0' || c > '9') return false;
                value = value * 10 + (c - '0');
            }
            return true;
        };
        if (digits(text, 0, 4, year) && digits(text, 5, 2, month) && digits(text, 8, 2, day) &&
            month >= 1 && month <= 12 && day >= 1 && day <= 31) {
            enabled = true;
            date = text;
            return {};
        }
        return "--newer-than '" + text + "' is not a valid date (expected YYYY-MM-DD)";
    }

    std::uintmax_t value = 0;
    std::size_t i = 0;
    bool any_digit = false;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        any_digit = true;
        const std::uintmax_t digit = static_cast<std::uintmax_t>(text[i] - '0');
        if (value > (std::numeric_limits<std::uintmax_t>::max() - digit) / 10) {
            return "--newer-than '" + text + "' is too large";
        }
        value = value * 10 + digit;
    }
    if (!any_digit || i + 1 != text.size()) {
        return "--newer-than must be a duration (7d, 12h, 30m) or a date (YYYY-MM-DD)";
    }

    std::uintmax_t seconds_per_unit = 0;
    switch (text[i]) {
        case 's':
            seconds_per_unit = 1;
            break;
        case 'm':
            seconds_per_unit = 60;
            break;
        case 'h':
            seconds_per_unit = 60 * 60;
            break;
        case 'd':
            seconds_per_unit = 24 * 60 * 60;
            break;
        case 'w':
            seconds_per_unit = 7 * 24 * 60 * 60;
            break;
        default:
            return "--newer-than unit must be one of s, m, h, d, w (got '" +
                   std::string(1, text[i]) + "')";
    }
    if (value > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max()) /
                    seconds_per_unit) {
        return "--newer-than '" + text + "' is too large";
    }
    enabled = true;
    window = std::chrono::seconds(static_cast<std::int64_t>(value * seconds_per_unit));
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
    std::string min_size_str;
    std::string max_size_str;
    std::string newer_than_str;

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

    app.add_option("--min-size", min_size_str,
                   "Minimum file size in bytes; accepts a k, M or G suffix (e.g. 10k, 5M)");
    app.add_option("--max-size", max_size_str,
                   "Maximum file size in bytes; accepts a k, M or G suffix (e.g. 10k, 5M)");
    app.add_option("--newer-than", newer_than_str,
                   "Only files modified within a window (7d, 12h, 30m) or on/after a date "
                   "(YYYY-MM-DD)");

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

    // Size bounds. The defaults are unbounded; --min-size/--max-size narrow them.
    std::uintmax_t min_size = 0;
    std::uintmax_t max_size = std::numeric_limits<std::uintmax_t>::max();
    if (!min_size_str.empty()) {
        const std::string error = parseSize(min_size_str, min_size);
        if (!error.empty()) return usageError(error);
    }
    if (!max_size_str.empty()) {
        const std::string error = parseSize(max_size_str, max_size);
        if (!error.empty()) return usageError(error);
    }
    if (min_size > max_size) {
        return usageError("--min-size (" + min_size_str + ") is larger than --max-size (" +
                          max_size_str + ")");
    }

    // --newer-than: a relative window or an absolute date.
    bool newer_than_enabled = false;
    std::chrono::seconds newer_than{0};
    std::string newer_than_date;
    if (!newer_than_str.empty()) {
        const std::string error =
            parseNewerThan(newer_than_str, newer_than_enabled, newer_than, newer_than_date);
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
    result.options.min_size = min_size;
    result.options.max_size = max_size;
    result.options.newer_than_enabled = newer_than_enabled;
    result.options.newer_than = newer_than;
    result.options.newer_than_date = std::move(newer_than_date);
    result.options.color = color_mode;
    return result;
}

}  // namespace hyperfinder
