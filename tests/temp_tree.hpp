#ifndef HYPERFINDER_TESTS_TEMP_TREE_HPP
#define HYPERFINDER_TESTS_TEMP_TREE_HPP

#include <filesystem>
#include <string>

namespace hf_test {

namespace fs = std::filesystem;

// A uniquely named directory under the system temp directory, removed
// recursively when it goes out of scope. Test fixtures must never touch a
// fixed path, so a failed run cannot leave state behind for the next one.
//
// Careful with links: std::filesystem::remove_all follows a Windows junction
// with libstdc++, so a test that creates one must unlink it before this object
// is destroyed (see LinkCleanup in test_file_searcher.cpp).
class TempTree {
public:
    // label only appears in the directory name; it makes failures readable.
    explicit TempTree(const std::string& label);
    ~TempTree();

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    const fs::path& root() const noexcept { return root_; }

    // Creates the parent directories as needed and returns the new path.
    fs::path makeDir(const std::string& relative);
    fs::path makeFile(const std::string& relative, const std::string& content = "");

    // Total number of regular files below the root, counted with a plain
    // single-threaded walk (the reference for the searcher's own counter).
    std::size_t countFiles() const;

private:
    fs::path root_;
};

// Lower-cases ASCII bytes only, matching what the searcher does.
std::string asciiLower(std::string value);

// Zero-padded decimal, so generated names sort in creation order.
std::string padded(int value, int width);

}  // namespace hf_test

#endif
