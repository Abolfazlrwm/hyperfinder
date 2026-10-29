#include "doctest/doctest.h"

#include "file_searcher.h"
#include "reference.hpp"
#include "temp_tree.hpp"

#include <string>
#include <vector>

using hf_test::describeDifference;
using hf_test::padded;
using hf_test::TempTree;

namespace {

// 64 directories x 40 matching files. This is the shape that produced
// 45/57/3/17/110 results with the original implementation.
constexpr int kDirectories = 64;
constexpr int kFilesPerDirectory = 40;
constexpr int kExpectedMatches = kDirectories * kFilesPerDirectory;
constexpr int kRuns = 200;

}  // namespace

// Regression test for the original race: the prototype returned before the
// worker pool had finished, so consecutive runs over the same tree reported
// wildly different result counts.
TEST_CASE("FileSearcher: 200 consecutive searches return identical results") {
    TempTree tree("stress");
    for (int dir = 0; dir < kDirectories; ++dir) {
        for (int file = 0; file < kFilesPerDirectory; ++file) {
            tree.makeFile("dir" + padded(dir, 2) + "/target_" + padded(file, 2) + ".txt");
        }
    }

    const std::vector<std::string> expected = hf_test::referenceMatches(tree.root(), "target");
    REQUIRE(expected.size() == static_cast<std::size_t>(kExpectedMatches));

    for (int run = 0; run < kRuns; ++run) {
        FileSearcher searcher("target", tree.root());
        searcher.search();

        const std::string difference = describeDifference(searcher.results(), expected);
        if (!difference.empty()) {
            FAIL_CHECK("run " << run << " differed: " << difference);
            return;
        }
    }

    CHECK(true);  // all 200 runs agreed with the reference
}
