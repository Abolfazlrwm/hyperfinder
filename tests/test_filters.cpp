#include "doctest/doctest.h"

#include <hyperfinder/file_searcher.h>

#include "temp_tree.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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
using hyperfinder::FileSearcher;
using hyperfinder::kUnlimitedDepth;
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
