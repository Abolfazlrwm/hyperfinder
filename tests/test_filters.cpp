#include "doctest/doctest.h"

#include <hyperfinder/file_searcher.h>

#include "temp_tree.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
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

using hf_test::TempTree;
using hyperfinder::ContentMatch;
using hyperfinder::FileSearcher;
using hyperfinder::kUnlimitedDepth;
using hyperfinder::MatchMode;
using hyperfinder::Options;

namespace {

namespace fs = std::filesystem;

Options optionsFor(const std::string& query, const fs::path& root) {
    Options options;
    options.query = query;
    options.root = root.u8string();
    return options;
}

std::vector<std::string> search(const Options& options) {
    FileSearcher searcher(options);
    searcher.search();
    return searcher.results();
}

std::vector<ContentMatch> contentSearch(const Options& options) {
    FileSearcher searcher(options);
    searcher.search();
    return searcher.contentMatches();
}

// Names, not full paths: the temp root differs on every run.
std::vector<std::string> baseNames(const std::vector<std::string>& paths) {
    std::vector<std::string> names;
    names.reserve(paths.size());
    for (const std::string& path : paths) {
        names.push_back(fs::u8path(path).filename().u8string());
    }
    return names;
}

// A Windows junction cannot be removed with remove_all (libstdc++ follows it and
// recurses until the path limit), so it is unlinked before the fixture dies.
struct LinkCleanup {
    fs::path link;
    ~LinkCleanup() {
        std::error_code ec;
        fs::remove(link, ec);
    }
};

}  // namespace

TEST_CASE("Filters: max_depth 0 reports only the entries inside the root") {
    TempTree tree("depth0");
    tree.makeFile("a.txt");
    tree.makeFile("sub/b.txt");
    tree.makeFile("sub/deep/c.txt");

    Options options = optionsFor("txt", tree.root());
    options.max_depth = 0;
    CHECK(baseNames(search(options)) == std::vector<std::string>{"a.txt"});

    FileSearcher searcher(options);
    searcher.search();
    CHECK(searcher.stats().directories_scanned == 1);  // the root only
}

TEST_CASE("Filters: max_depth 1 descends exactly one level") {
    TempTree tree("depth1");
    tree.makeFile("a.txt");
    tree.makeFile("sub/b.txt");
    tree.makeFile("sub/deep/c.txt");

    Options options = optionsFor("txt", tree.root());
    options.max_depth = 1;
    const std::vector<std::string> names = baseNames(search(options));

    CHECK(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), "a.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "b.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "c.txt") == names.end());
}

TEST_CASE("Filters: the default depth is unlimited") {
    TempTree tree("depthall");
    tree.makeFile("a.txt");
    tree.makeFile("sub/b.txt");
    tree.makeFile("sub/deep/c.txt");

    const Options options = optionsFor("txt", tree.root());
    CHECK(options.max_depth == kUnlimitedDepth);
    CHECK(search(options).size() == 3);
}

TEST_CASE("Filters: --ext keeps only the requested extensions") {
    TempTree tree("ext");
    tree.makeFile("a.txt");
    tree.makeFile("b.log");
    tree.makeFile("c.TXT");
    tree.makeFile("d.tar.gz");

    Options options = optionsFor("", tree.root());
    options.extensions = {".txt"};
    const std::vector<std::string> names = baseNames(search(options));

    CHECK(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), "a.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "c.TXT") != names.end());
}

TEST_CASE("Filters: --ext matches the last extension only") {
    TempTree tree("extlast");
    tree.makeFile("archive.tar.gz");
    tree.makeFile("plain.tar");

    Options options = optionsFor("", tree.root());
    options.extensions = {".gz"};

    CHECK(baseNames(search(options)) == std::vector<std::string>{"archive.tar.gz"});
}

TEST_CASE("Filters: an --exclude glob hides matching files") {
    TempTree tree("exfile");
    tree.makeFile("keep.txt");
    tree.makeFile("drop.tmp");
    tree.makeFile("also.log");

    Options options = optionsFor("", tree.root());
    options.exclude = {"*.tmp", "*.log"};

    CHECK(baseNames(search(options)) == std::vector<std::string>{"keep.txt"});
}

TEST_CASE("Filters: an excluded directory is never descended into") {
    TempTree tree("exdir");
    tree.makeFile("keep.txt");
    tree.makeFile("node_modules/pkg/index.js");
    tree.makeFile("node_modules/pkg/deep/x.txt");

    Options options = optionsFor("txt", tree.root());
    options.exclude = {"node_modules"};

    CHECK(baseNames(search(options)) == std::vector<std::string>{"keep.txt"});

    FileSearcher searcher(options);
    searcher.search();
    // The pruned directory is not counted as scanned, because it was never
    // opened.
    CHECK(searcher.stats().directories_scanned == 1);
}

TEST_CASE("Filters: a dotfile is hidden and skipped by default") {
    TempTree tree("hidden");
    tree.makeFile("visible.txt");
    tree.makeFile(".hidden.txt");

    Options options = optionsFor("txt", tree.root());
    CHECK(baseNames(search(options)) == std::vector<std::string>{"visible.txt"});

    options.include_hidden = true;
    const std::vector<std::string> names = baseNames(search(options));
    CHECK(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), ".hidden.txt") != names.end());
}

TEST_CASE("Filters: a hidden directory is not descended into by default") {
    TempTree tree("hiddendir");
    tree.makeFile("keep.txt");
    tree.makeFile(".git/config.txt");

    Options options = optionsFor("txt", tree.root());
    CHECK(baseNames(search(options)) == std::vector<std::string>{"keep.txt"});

    options.include_hidden = true;
    CHECK(search(options).size() == 2);
}

#if defined(_WIN32)
TEST_CASE("Filters: the Windows hidden attribute is honoured") {
    TempTree tree("winattr");
    const fs::path visible = tree.makeFile("visible.txt");
    const fs::path marked = tree.makeFile("marked.txt");

    // A plain name plus FILE_ATTRIBUTE_HIDDEN: only the Win32 branch can see
    // this one, since the name has no leading dot.
    if (::SetFileAttributesW(marked.c_str(), FILE_ATTRIBUTE_HIDDEN) == 0) {
        std::cout << "[ skip ] Windows hidden attribute test: cannot set the attribute\n";
        return;
    }

    Options options = optionsFor("txt", tree.root());
    CHECK(baseNames(search(options)) == std::vector<std::string>{"visible.txt"});

    options.include_hidden = true;
    const std::vector<std::string> names = baseNames(search(options));
    CHECK(names.size() == 2);

    CHECK(fs::exists(visible));
    ::SetFileAttributesW(marked.c_str(), FILE_ATTRIBUTE_NORMAL);
}
#endif

TEST_CASE("Filters: Persian, accented and emoji names match") {
    TempTree tree("unicode");
    tree.makeFile("علی.txt");
    tree.makeFile("café.txt");
    tree.makeFile("\xF0\x9F\x9A\x80.txt");  // U+1F680 rocket
    tree.makeFile("plain.txt");

    CHECK(baseNames(search(optionsFor("علی", tree.root()))) == std::vector<std::string>{"علی.txt"});
    CHECK(baseNames(search(optionsFor("café", tree.root()))) ==
          std::vector<std::string>{"café.txt"});
    CHECK(baseNames(search(optionsFor("\xF0\x9F\x9A\x80", tree.root()))) ==
          std::vector<std::string>{"\xF0\x9F\x9A\x80.txt"});
    CHECK(search(optionsFor("txt", tree.root())).size() == 4);
}

TEST_CASE("Filters: an --exclude glob matches a non-ASCII name") {
    TempTree tree("unicodeglob");
    tree.makeFile("علی.txt");
    tree.makeFile("keep.txt");

    Options options = optionsFor("txt", tree.root());
    options.exclude = {"???.*"};  // three code points, then a dot

    CHECK(baseNames(search(options)) == std::vector<std::string>{"keep.txt"});
}

// ---------------------------------------------------------------------------
// Phase 5: matching modes
// ---------------------------------------------------------------------------

TEST_CASE("Matching: a glob query matches by wildcard") {
    TempTree tree("globmode");
    tree.makeFile("a.txt");
    tree.makeFile("a.log");
    tree.makeFile("b.txt");

    Options options = optionsFor("*.txt", tree.root());
    options.match_mode = MatchMode::Glob;

    const std::vector<std::string> names = baseNames(search(options));
    CHECK(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), "a.txt") != names.end());
    CHECK(std::find(names.begin(), names.end(), "b.txt") != names.end());
}

TEST_CASE("Matching: a glob is case-insensitive under the default -i") {
    TempTree tree("globci");
    tree.makeFile("A.TXT");
    tree.makeFile("b.log");

    Options options = optionsFor("*.txt", tree.root());
    options.match_mode = MatchMode::Glob;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"A.TXT"});
}

TEST_CASE("Matching: a regex query matches the file name") {
    TempTree tree("regexmode");
    tree.makeFile("build.log");
    tree.makeFile("build.txt");
    tree.makeFile("run.sh");

    Options options = optionsFor("^build\\..*txt$", tree.root());
    options.match_mode = MatchMode::Regex;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"build.txt"});
}

TEST_CASE("Matching: a regex honours --case-sensitive") {
    TempTree tree("regexcs");
    tree.makeFile("Alpha.TXT");
    tree.makeFile("beta.txt");

    Options options = optionsFor("alpha", tree.root());
    options.match_mode = MatchMode::Regex;
    options.ignore_case = false;

    CHECK(search(options).size() == 0);
}

TEST_CASE("Matching: a Persian regex matches a Persian name") {
    TempTree tree("regexpersian");
    tree.makeFile("علی.txt");
    tree.makeFile("café.txt");

    Options options = optionsFor("علی", tree.root());
    options.match_mode = MatchMode::Regex;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"علی.txt"});
}

TEST_CASE("Matching: an invalid regex is reported as an error") {
    // The pattern is compiled in the FileSearcher constructor, so the failure
    // surfaces before any directory is touched. The root is irrelevant here.
    Options options = optionsFor("[", fs::path("."));
    options.match_mode = MatchMode::Regex;

    CHECK_THROWS_AS(FileSearcher searcher(options), std::regex_error);
}

// ---------------------------------------------------------------------------
// Phase 5: size and modification-time filters
// ---------------------------------------------------------------------------

TEST_CASE("Filters: --min-size keeps only large enough files") {
    TempTree tree("minsize");
    tree.makeFile("small.txt", std::string(10, 'a'));
    tree.makeFile("big.txt", std::string(100, 'b'));

    Options options = optionsFor("txt", tree.root());
    options.min_size = 50;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"big.txt"});
}

TEST_CASE("Filters: --max-size keeps only small enough files") {
    TempTree tree("maxsize");
    tree.makeFile("small.txt", std::string(10, 'a'));
    tree.makeFile("big.txt", std::string(100, 'b'));

    Options options = optionsFor("txt", tree.root());
    options.max_size = 50;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"small.txt"});
}

TEST_CASE("Filters: size bounds are inclusive") {
    TempTree tree("sizeinclusive");
    tree.makeFile("exact.txt", std::string(50, 'a'));

    Options options = optionsFor("txt", tree.root());
    options.min_size = 50;
    options.max_size = 50;

    CHECK(baseNames(search(options)) == std::vector<std::string>{"exact.txt"});
}

TEST_CASE("Filters: --newer-than excludes an old file") {
    TempTree tree("newerrel");
    const fs::path old_file = tree.makeFile("old.txt", "x");
    tree.makeFile("new.txt", "x");

    // Push one file ten days into the past; the other keeps its creation time.
    std::error_code ec;
    fs::last_write_time(old_file, fs::file_time_type::clock::now() - std::chrono::hours(24 * 10),
                        ec);
    REQUIRE_FALSE(ec);

    Options options = optionsFor("txt", tree.root());
    options.newer_than_enabled = true;
    options.newer_than = std::chrono::hours(24 * 7);  // one week

    CHECK(baseNames(search(options)) == std::vector<std::string>{"new.txt"});
}

TEST_CASE("Filters: --newer-than accepts an ISO date") {
    TempTree tree("newerdate");
    tree.makeFile("a.txt", "x");

    Options past = optionsFor("txt", tree.root());
    past.newer_than_enabled = true;
    past.newer_than_date = "2000-01-01";
    CHECK(search(past).size() == 1);

    Options future = optionsFor("txt", tree.root());
    future.newer_than_enabled = true;
    future.newer_than_date = "2999-01-01";
    CHECK(search(future).size() == 0);
}

TEST_CASE("Filters: an invalid --newer-than date is rejected") {
    Options options = optionsFor("txt", fs::path("."));
    options.newer_than_enabled = true;
    options.newer_than_date = "2026-02-31";  // February has no 31st

    CHECK_THROWS_AS(FileSearcher searcher(options), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Phase 5: content search
// ---------------------------------------------------------------------------

TEST_CASE("Content: reports the matching line and its number") {
    TempTree tree("contentlines");
    tree.makeFile("a.txt", "first\nsecond needle\nthird\n");

    Options options = optionsFor("", tree.root());
    options.content = "needle";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].line == 2);
    CHECK(matches[0].text == "second needle");
}

TEST_CASE("Content: a match split across a chunk boundary is found") {
    TempTree tree("contentboundary");
    // kContentChunk is 64 KiB; place the needle so it straddles that offset.
    tree.makeFile("boundary.txt", std::string(65530, 'x') + "NEEDLE\n");

    Options options = optionsFor("", tree.root());
    options.content = "NEEDLE";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].line == 1);
}

TEST_CASE("Content: a CRLF line is reported without the CR") {
    TempTree tree("contentcrlf");
    tree.makeFile("crlf.txt", "line1\r\nline2\r\n");

    Options options = optionsFor("", tree.root());
    options.content = "line2";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].line == 2);
    CHECK(matches[0].text == "line2");
}

TEST_CASE("Content: an empty file has no matches") {
    TempTree tree("contentempty");
    tree.makeFile("empty.txt", "");

    Options options = optionsFor("", tree.root());
    options.content = "anything";

    CHECK(contentSearch(options).empty());
}

TEST_CASE("Content: a binary file is skipped") {
    TempTree tree("contentbinary");
    tree.makeFile("data.bin", std::string("a\0b\n", 4));

    Options options = optionsFor("", tree.root());
    options.content = "a";

    CHECK(contentSearch(options).empty());
}

TEST_CASE("Content: Persian text matches") {
    TempTree tree("contentpersian");
    tree.makeFile("fa.txt", "سلام دنیا\nخط دوم\n");

    Options options = optionsFor("", tree.root());
    options.content = "سلام";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].line == 1);
    CHECK(matches[0].text == "سلام دنیا");
}

TEST_CASE("Content: the last line without a trailing newline is searched") {
    TempTree tree("contentnonl");
    tree.makeFile("nonl.txt", "abc");

    Options options = optionsFor("", tree.root());
    options.content = "abc";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].line == 1);
    CHECK(matches[0].text == "abc");
}

TEST_CASE("Content: a long line is truncated") {
    TempTree tree("contentlong");
    tree.makeFile("long.txt", std::string(500, 'x') + "\n");

    Options options = optionsFor("", tree.root());
    options.content = "x";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(matches[0].text.size() == 203);  // 200 bytes + "..."
    CHECK(matches[0].text.substr(200) == "...");
}

TEST_CASE("Content: case-insensitive by default, sensitive with --case-sensitive") {
    TempTree tree("contentcase");
    tree.makeFile("case.txt", "Hello World\n");

    Options insensitive = optionsFor("", tree.root());
    insensitive.content = "hello";
    CHECK(contentSearch(insensitive).size() == 1);

    Options sensitive = optionsFor("", tree.root());
    sensitive.content = "hello";
    sensitive.ignore_case = false;
    CHECK(contentSearch(sensitive).empty());
}

TEST_CASE("Content: name filters still apply") {
    TempTree tree("contentname");
    tree.makeFile("a.txt", "needle\n");
    tree.makeFile("b.log", "needle\n");

    Options options = optionsFor("", tree.root());
    options.content = "needle";
    options.extensions = {".txt"};

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 1);
    CHECK(fs::u8path(matches[0].path).filename().u8string() == "a.txt");
}

TEST_CASE("Content: multiple matching lines are sorted by path then line") {
    TempTree tree("contentmulti");
    tree.makeFile("b.txt", "hit\nhit\n");
    tree.makeFile("a.txt", "hit\n");

    Options options = optionsFor("", tree.root());
    options.content = "hit";

    const std::vector<ContentMatch> matches = contentSearch(options);
    REQUIRE(matches.size() == 3);
    CHECK(fs::u8path(matches[0].path).filename().u8string() == "a.txt");
    CHECK(matches[0].line == 1);
    CHECK(fs::u8path(matches[1].path).filename().u8string() == "b.txt");
    CHECK(matches[1].line == 1);
    CHECK(matches[2].line == 2);
}

TEST_CASE("Filters: results are sorted") {
    TempTree tree("sorted");
    tree.makeFile("c_match.txt");
    tree.makeFile("a_match.txt");
    tree.makeFile("b_match.txt");

    const std::vector<std::string> results = search(optionsFor("match", tree.root()));
    REQUIRE(results.size() == 3);
    CHECK(std::is_sorted(results.begin(), results.end()));
}

TEST_CASE("Filters: following a directory link terminates and reports no duplicates") {
    TempTree tree("follow");
    tree.makeFile("a/b/file.txt");

    const fs::path link = tree.root() / "a" / "loop";
    std::error_code ec;
    fs::create_directory_symlink(tree.root(), link, ec);
    if (ec || !fs::exists(link)) {
        // libstdc++ on Windows has no create_directory_symlink, so fall back to
        // a junction there.
#if defined(_WIN32)
        const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" +
                                    tree.root().string() + "\" >nul 2>&1";
        if (std::system(command.c_str()) != 0 || !fs::exists(link)) {
            std::cout << "[ skip ] follow-symlinks test: cannot create a link here\n";
            return;
        }
#else
        std::cout << "[ skip ] follow-symlinks test: cannot create a link here ("
                  << ec.message() << ")\n";
        return;
#endif
    }
    LinkCleanup cleanup{link};

    Options options = optionsFor("file", tree.root());
    options.follow_symlinks = true;

    const std::vector<std::string> results = search(options);
    // One real file. Following the loop back to the root must not multiply it.
    CHECK(results.size() == 1);
}

TEST_CASE("Filters: a directory link is not followed by default") {
    TempTree tree("nofollow");
    tree.makeFile("a/b/file.txt");

    const fs::path link = tree.root() / "a" / "loop";
    std::error_code ec;
    fs::create_directory_symlink(tree.root(), link, ec);
    if (ec || !fs::exists(link)) {
#if defined(_WIN32)
        const std::string command = "cmd /c mklink /J \"" + link.string() + "\" \"" +
                                    tree.root().string() + "\" >nul 2>&1";
        if (std::system(command.c_str()) != 0 || !fs::exists(link)) {
            std::cout << "[ skip ] no-follow test: cannot create a link here\n";
            return;
        }
#else
        std::cout << "[ skip ] no-follow test: cannot create a link here (" << ec.message()
                  << ")\n";
        return;
#endif
    }
    LinkCleanup cleanup{link};

    CHECK(search(optionsFor("file", tree.root())).size() == 1);
}
