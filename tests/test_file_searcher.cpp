#include "doctest/doctest.h"

#include <hyperfinder/file_searcher.h>
#include "reference.hpp"
#include "temp_tree.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using hf_test::describeDifference;
using hf_test::padded;
using hf_test::TempTree;

namespace {

// A tree of 64 directories x 40 matching files, the same shape used to
// reproduce the original race.
void buildReferenceTree(TempTree& tree) {
    for (int dir = 0; dir < 64; ++dir) {
        for (int file = 0; file < 40; ++file) {
            tree.makeFile("dir" + padded(dir, 2) + "/target_" + padded(file, 2) + ".txt");
        }
    }
}

// std::filesystem::remove_all follows a Windows junction with libstdc++ and
// recurses until the path limit, so a test that creates one must unlink it
// before the fixture directory is removed. Declare this after the TempTree so
// it is destroyed first.
struct LinkCleanup {
    fs::path link;

    ~LinkCleanup() {
        std::error_code ec;
        fs::remove(link, ec);
    }
};

}  // namespace

TEST_CASE("FileSearcher: results match a single-threaded reference walk") {
    TempTree tree("reference");
    buildReferenceTree(tree);
    tree.makeFile("unrelated.txt");
    tree.makeFile("dir00/other.log");

    FileSearcher searcher("target", tree.root());
    searcher.search();

    const std::vector<std::string> expected = hf_test::referenceMatches(tree.root(), "target");
    REQUIRE(expected.size() == 2560);

    const std::string difference = describeDifference(searcher.results(), expected);
    CHECK_MESSAGE(difference.empty(), difference);
}

TEST_CASE("FileSearcher: statistics separate files from directories") {
    TempTree tree("stats");
    for (int dir = 0; dir < 10; ++dir) {
        for (int file = 0; file < 5; ++file) {
            tree.makeFile("d" + padded(dir, 2) + "/f" + padded(file, 2) + ".txt");
        }
    }

    FileSearcher searcher("f", tree.root());
    searcher.search();

    const SearchStats stats = searcher.stats();
    CHECK(stats.files_scanned == 50);
    CHECK(stats.directories_scanned == 11);  // ten subdirectories plus the root
    CHECK(stats.paths_skipped == 0);
    CHECK(tree.countFiles() == 50);
}

TEST_CASE("FileSearcher: matching is case-insensitive") {
    TempTree tree("case");
    tree.makeFile("Alpha.TXT");
    tree.makeFile("beta.txt");
    tree.makeFile("GAMMA.log");

    const auto matchesFor = [&tree](const std::string& needle) {
        FileSearcher searcher(needle, tree.root());
        searcher.search();
        return searcher.results();
    };

    CHECK(matchesFor("alpha").size() == 1);
    CHECK(matchesFor("ALPHA").size() == 1);
    CHECK(matchesFor("AlPhA").size() == 1);
    CHECK(matchesFor("txt").size() == 2);
    CHECK(matchesFor("gamma").size() == 1);
    CHECK(matchesFor("nothing-here").empty());
}

TEST_CASE("FileSearcher: an empty directory yields no results") {
    TempTree tree("empty");

    FileSearcher searcher("anything", tree.root());
    searcher.search();

    CHECK(searcher.results().empty());
    CHECK(searcher.stats().files_scanned == 0);
    CHECK(searcher.stats().directories_scanned == 1);
}

TEST_CASE("FileSearcher: a root that is not a directory is an error") {
    TempTree tree("badroot");
    const fs::path file = tree.makeFile("plain.txt");

    FileSearcher missing("x", tree.root() / "does-not-exist");
    CHECK_THROWS_AS(missing.search(), std::runtime_error);

    FileSearcher notADir("x", file);
    CHECK_THROWS_AS(notADir.search(), std::runtime_error);
}

TEST_CASE("FileSearcher: reaches a file far down a deep tree") {
    TempTree tree("deep");

    // Windows cannot open paths longer than MAX_PATH (260) without a \\?\ prefix,
    // so the depth is chosen to keep the whole path comfortably below that.
    // Under %TEMP% this yields roughly 80-90 levels; see the README limitations.
    constexpr std::size_t kPathBudget = 240;
    const std::size_t base_length = tree.root().string().size();
    REQUIRE(base_length < kPathBudget);
    const int levels =
        static_cast<int>(std::min<std::size_t>(100, (kPathBudget - base_length) / 2));
    REQUIRE(levels >= 20);

    std::string relative;
    for (int level = 0; level < levels; ++level) {
        relative += "d/";
    }
    tree.makeFile(relative + "needle.txt");

    FileSearcher searcher("needle", tree.root());
    searcher.search();

    CHECK(searcher.results().size() == 1);
    CHECK(searcher.stats().directories_scanned == static_cast<std::uint64_t>(levels) + 1);
    CHECK(searcher.stats().paths_skipped == 0);
}

TEST_CASE("FileSearcher: handles 3000 files in a single directory") {
    TempTree tree("wide");
    for (int file = 0; file < 3000; ++file) {
        tree.makeFile("w_" + padded(file, 4) + ".txt");
    }

    FileSearcher searcher("w_", tree.root());
    searcher.search();

    CHECK(searcher.results().size() == 3000);
    CHECK(searcher.stats().files_scanned == 3000);
    CHECK(searcher.stats().directories_scanned == 1);
}

TEST_CASE("FileSearcher: a symlink cycle does not cause an endless scan") {
    TempTree tree("symlink");
    tree.makeFile("a/b/file.txt");

    const fs::path link = tree.root() / "a" / "loop";
    std::error_code ec;
    fs::create_directory_symlink(tree.root(), link, ec);
    if (ec || !fs::exists(link)) {
        // Printed directly: doctest hides MESSAGE/WARN_MESSAGE unless the run is
        // verbose, and a skipped test should be visible in a plain ctest run.
        std::cout << "[ skip ] symlink cycle test: cannot create directory symlinks here ("
                  << ec.message() << ")\n";
        return;
    }

    FileSearcher searcher("file", tree.root());
    searcher.search();

    // One real file, not one per traversal level.
    CHECK(searcher.results().size() == 1);
}

#if defined(_WIN32)
TEST_CASE("FileSearcher: a Windows junction cycle does not cause an endless scan") {
    TempTree tree("junction");
    tree.makeFile("a/b/file.txt");

    const fs::path link = tree.root() / "a" / "loop";
    // Junctions can be created without administrator rights, unlike symlinks,
    // and there is no portable API for them. libstdc++ also reports a junction
    // as a plain directory, which is exactly the case the searcher has to catch.
    const std::string command =
        "cmd /c mklink /J \"" + link.string() + "\" \"" + tree.root().string() + "\" >nul 2>&1";
    const int result = std::system(command.c_str());
    if (result != 0 || !fs::exists(link)) {
        std::cout << "[ skip ] junction cycle test: cannot create a junction here (exit code "
                  << result << ")\n";
        return;
    }
    LinkCleanup cleanup{link};

    FileSearcher searcher("file", tree.root());
    searcher.search();

    CHECK(searcher.results().size() == 1);
}
#endif
