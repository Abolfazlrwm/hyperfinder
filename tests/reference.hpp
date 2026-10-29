#ifndef HYPERFINDER_TESTS_REFERENCE_HPP
#define HYPERFINDER_TESTS_REFERENCE_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace hf_test {

// Single-threaded, deliberately boring reference implementation: one
// recursive_directory_iterator walk, links skipped, case-insensitive substring
// match on the file name, sorted native paths. The threaded searcher has to
// produce exactly this.
std::vector<std::string> referenceMatches(const std::filesystem::path& root,
                                          const std::string& needle);

// Empty when both lists are equal; otherwise a short description of the first
// difference, so a failing CHECK does not dump thousands of paths.
std::string describeDifference(const std::vector<std::string>& actual,
                               const std::vector<std::string>& expected);

}  // namespace hf_test

#endif
