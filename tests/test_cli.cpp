#include <doctest/doctest.h>

#include <hyperfinder/cli.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace {

// The command line always carries the program name at index 0.
hyperfinder::ParseResult parse(std::initializer_list<const char*> arguments) {
    std::vector<std::string> args{"hyperfinder"};
    for (const char* argument : arguments) args.emplace_back(argument);
    return hyperfinder::parseArguments(args);
}

}  // namespace

TEST_CASE("a minimal command line produces a runnable configuration") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK(result.exit_code == 0);
    CHECK(result.options.query == "needle");
    CHECK(result.options.root == ".");
    CHECK(result.options.threads == 0);
    CHECK(result.options.ignore_case);
    CHECK(result.stderr_text.empty());
}

TEST_CASE("the root directory can be given positionally") {
    const auto result = parse({"needle", "C:/dev"});

    REQUIRE(result.should_run);
    CHECK(result.options.root == "C:/dev");
}

TEST_CASE("-j sets the worker count") {
    const auto result = parse({"-j", "4", "needle", "."});

    REQUIRE(result.should_run);
    CHECK(result.options.threads == 4);
}

TEST_CASE("--threads and -j are the same option") {
    CHECK(parse({"--threads", "3", "needle"}).options.threads == 3);
    CHECK(parse({"-j", "3", "needle"}).options.threads == 3);
}

TEST_CASE("an absurd thread count is rejected") {
    const auto result = parse({"-j", "5000", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
    CHECK(result.stderr_text.find("1024") != std::string::npos);
}

TEST_CASE("--case-sensitive turns off the default case folding") {
    const auto result = parse({"--case-sensitive", "needle"});

    REQUIRE(result.should_run);
    CHECK_FALSE(result.options.ignore_case);
}

TEST_CASE("-i keeps case folding on") {
    const auto result = parse({"-i", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.ignore_case);
}

TEST_CASE("asking for both case modes is a usage error") {
    const auto result = parse({"-i", "--case-sensitive", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
    CHECK_FALSE(result.stderr_text.empty());
}

TEST_CASE("a missing query is a usage error") {
    const auto result = parse({});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
    CHECK_FALSE(result.stderr_text.empty());
}

TEST_CASE("an unknown option is a usage error") {
    const auto result = parse({"--frobnicate", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
    CHECK_FALSE(result.stderr_text.empty());
}

TEST_CASE("too many positional arguments is a usage error") {
    const auto result = parse({"needle", "path", "surplus"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

TEST_CASE("--help succeeds and explains the output contract") {
    const auto result = parse({"--help"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 0);
    CHECK(result.stderr_text.empty());
    CHECK(result.stdout_text.find("hyperfinder") != std::string::npos);
    CHECK(result.stdout_text.find("[OPTIONS]") != std::string::npos);
    CHECK(result.stdout_text.find("stdout") != std::string::npos);
    CHECK(result.stdout_text.find("Exit codes") != std::string::npos);
}

TEST_CASE("-h is the same as --help") {
    const auto short_form = parse({"-h"});
    const auto long_form = parse({"--help"});

    CHECK(short_form.exit_code == 0);
    CHECK(short_form.stdout_text == long_form.stdout_text);
}

TEST_CASE("--version prints a version and succeeds") {
    const auto result = parse({"--version"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 0);
    CHECK(result.stdout_text.find("hyperfinder") == 0);
    CHECK(result.stdout_text.back() == '\n');
}

TEST_CASE("a query that starts with a dash needs the -- separator") {
    const auto result = parse({"--", "-dash", "."});

    REQUIRE(result.should_run);
    CHECK(result.options.query == "-dash");
    CHECK(result.options.root == ".");
}

TEST_CASE("an empty argument vector is treated as a bare invocation") {
    const auto result = hyperfinder::parseArguments({});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

// ---------------------------------------------------------------------------
// Phase 4: filtering and output options
// ---------------------------------------------------------------------------

TEST_CASE("--max-depth sets the depth limit") {
    const auto result = parse({"--max-depth", "3", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.max_depth == 3);
}

TEST_CASE("--max-depth defaults to unlimited") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.max_depth == hyperfinder::kUnlimitedDepth);
}

TEST_CASE("--max-depth 0 is a real depth, not 'unlimited'") {
    const auto result = parse({"--max-depth", "0", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.max_depth == 0);
}

TEST_CASE("--exclude accepts a single glob") {
    const auto result = parse({"--exclude", "*.tmp", "needle"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.exclude.size() == 1);
    CHECK(result.options.exclude[0] == "*.tmp");
}

TEST_CASE("--exclude can be repeated") {
    const auto result = parse({"--exclude", "*.tmp", "--exclude", "*.log", "needle"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.exclude.size() == 2);
    CHECK(result.options.exclude[0] == "*.tmp");
    CHECK(result.options.exclude[1] == "*.log");
}

TEST_CASE("--ext accepts a single extension") {
    const auto result = parse({"--ext", ".cpp", "needle"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.extensions.size() == 1);
    CHECK(result.options.extensions[0] == ".cpp");
}

TEST_CASE("--ext can be repeated and accepts no leading dot") {
    const auto result = parse({"--ext", "cpp", "--ext", "h", "needle"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.extensions.size() == 2);
    CHECK(result.options.extensions[0] == ".cpp");
    CHECK(result.options.extensions[1] == ".h");
}

TEST_CASE("--ext is normalised to a leading dot and lower case") {
    const auto result = parse({"--ext", "CPP", "--ext", ".Txt", "needle"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.extensions.size() == 2);
    CHECK(result.options.extensions[0] == ".cpp");
    CHECK(result.options.extensions[1] == ".txt");
}

TEST_CASE("an empty --ext is dropped rather than matching nothing") {
    const auto result = parse({"--ext", "", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.extensions.empty());
}

// Regression: a vector option is greedy by default in CLI11 and used to eat the
// optional path positional, so "--ext log . dir" searched the current directory
// for the literal path. The optional positional is what makes this visible; the
// required query alone was enough to stop the greedy consumption.
TEST_CASE("--ext does not swallow the positional arguments") {
    const auto result = parse({"--ext", "cpp", ".", "C:/data"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.extensions.size() == 1);
    CHECK(result.options.extensions[0] == ".cpp");
    CHECK(result.options.query == ".");
    CHECK(result.options.root == "C:/data");
}

TEST_CASE("repeated --ext still leaves the positionals alone") {
    const auto result = parse({"--ext", "cpp", "--ext", "h", ".", "C:/data"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.extensions.size() == 2);
    CHECK(result.options.query == ".");
    CHECK(result.options.root == "C:/data");
}

TEST_CASE("--exclude does not swallow the positional arguments") {
    const auto result = parse({"--exclude", "*.tmp", "needle", "C:/data"});

    REQUIRE(result.should_run);
    REQUIRE(result.options.exclude.size() == 1);
    CHECK(result.options.query == "needle");
    CHECK(result.options.root == "C:/data");
}

TEST_CASE("--hidden enables hidden file traversal") {
    const auto result = parse({"--hidden", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.include_hidden);
}

TEST_CASE("hidden files are excluded by default") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK_FALSE(result.options.include_hidden);
}

TEST_CASE("--follow-symlinks enables symlink following") {
    const auto result = parse({"--follow-symlinks", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.follow_symlinks);
}

TEST_CASE("symlinks are not followed by default") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK_FALSE(result.options.follow_symlinks);
}

TEST_CASE("--count enables count-only output") {
    const auto result = parse({"--count", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.count_only);
}

TEST_CASE("--no-color disables colour") {
    const auto result = parse({"--no-color", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.color == hyperfinder::ColorMode::Never);
}

TEST_CASE("--color=always forces colour on") {
    const auto result = parse({"--color=always", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.color == hyperfinder::ColorMode::Always);
}

TEST_CASE("--color=never disables colour") {
    const auto result = parse({"--color=never", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.color == hyperfinder::ColorMode::Never);
}

TEST_CASE("--color=auto is the default") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.color == hyperfinder::ColorMode::Auto);
}

TEST_CASE("an invalid --color value is a usage error") {
    const auto result = parse({"--color=bright", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
    CHECK_FALSE(result.stderr_text.empty());
}

TEST_CASE("--no-color and --color=always are mutually exclusive") {
    const auto result = parse({"--no-color", "--color=always", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

TEST_CASE("--help lists the new options") {
    const auto result = parse({"--help"});

    CHECK(result.exit_code == 0);
    CHECK(result.stdout_text.find("--max-depth") != std::string::npos);
    CHECK(result.stdout_text.find("--exclude") != std::string::npos);
    CHECK(result.stdout_text.find("--ext") != std::string::npos);
    CHECK(result.stdout_text.find("--hidden") != std::string::npos);
    CHECK(result.stdout_text.find("--follow-symlinks") != std::string::npos);
    CHECK(result.stdout_text.find("--count") != std::string::npos);
    CHECK(result.stdout_text.find("--color") != std::string::npos);
    CHECK(result.stdout_text.find("--min-size") != std::string::npos);
    CHECK(result.stdout_text.find("--max-size") != std::string::npos);
    CHECK(result.stdout_text.find("--newer-than") != std::string::npos);
    CHECK(result.stdout_text.find("--glob") != std::string::npos);
    CHECK(result.stdout_text.find("--regex") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Phase 5: matching modes
// ---------------------------------------------------------------------------

TEST_CASE("the default match mode is substring") {
    const auto result = parse({"needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.match_mode == hyperfinder::MatchMode::Substring);
}

TEST_CASE("--glob selects glob matching") {
    const auto result = parse({"--glob", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.match_mode == hyperfinder::MatchMode::Glob);
}

TEST_CASE("--regex selects regex matching") {
    const auto result = parse({"--regex", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.match_mode == hyperfinder::MatchMode::Regex);
}

TEST_CASE("--glob and --regex cannot be combined") {
    const auto result = parse({"--glob", "--regex", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

// ---------------------------------------------------------------------------
// Phase 5: size and modification-time filters
// ---------------------------------------------------------------------------

TEST_CASE("--min-size and --max-size parse decimal units") {
    const auto result = parse({"--min-size", "10k", "--max-size", "5M", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.min_size == 10000);
    CHECK(result.options.max_size == 5000000);
}

TEST_CASE("--min-size accepts a G suffix and a trailing b") {
    const auto gigabytes = parse({"--min-size", "2G", "needle"});
    REQUIRE(gigabytes.should_run);
    CHECK(gigabytes.options.min_size == 2000000000);

    const auto kilobytes = parse({"--max-size", "10kb", "needle"});
    REQUIRE(kilobytes.should_run);
    CHECK(kilobytes.options.max_size == 10000);
}

TEST_CASE("an invalid size is a usage error") {
    const auto result = parse({"--min-size", "abc", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

TEST_CASE("--min-size larger than --max-size is a usage error") {
    const auto result = parse({"--min-size", "10M", "--max-size", "1k", "needle"});

    CHECK_FALSE(result.should_run);
    CHECK(result.exit_code == 2);
}

TEST_CASE("--newer-than parses a duration") {
    const auto result = parse({"--newer-than", "7d", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.newer_than_enabled);
    CHECK(result.options.newer_than.count() == 7 * 24 * 3600);
    CHECK(result.options.newer_than_date.empty());
}

TEST_CASE("--newer-than parses an ISO date") {
    const auto result = parse({"--newer-than", "2026-01-31", "needle"});

    REQUIRE(result.should_run);
    CHECK(result.options.newer_than_enabled);
    CHECK(result.options.newer_than_date == "2026-01-31");
}

TEST_CASE("an invalid --newer-than is a usage error") {
    const auto bad_unit = parse({"--newer-than", "7x", "needle"});
    CHECK(bad_unit.exit_code == 2);

    const auto bad_date = parse({"--newer-than", "2026-13-01", "needle"});
    CHECK(bad_date.exit_code == 2);
}
