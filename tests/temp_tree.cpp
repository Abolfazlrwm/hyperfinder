#include "temp_tree.hpp"

#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace hf_test {
namespace {

std::string uniqueSuffix() {
    static std::atomic<unsigned long> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1));
}

}  // namespace

TempTree::TempTree(const std::string& label) {
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) throw std::runtime_error("cannot locate the temp directory: " + ec.message());

    root_ = base / ("hyperfinder_test_" + label + "_" + uniqueSuffix());
    fs::create_directories(root_, ec);
    if (ec) throw std::runtime_error("cannot create " + root_.string() + ": " + ec.message());
}

TempTree::~TempTree() {
    std::error_code ec;
    fs::remove_all(root_, ec);  // best effort: a destructor must not throw
}

fs::path TempTree::makeDir(const std::string& relative) {
    fs::path path = root_ / relative;
    path.make_preferred();

    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) throw std::runtime_error("cannot create " + path.string() + ": " + ec.message());
    return path;
}

fs::path TempTree::makeFile(const std::string& relative, const std::string& content) {
    fs::path path = root_ / relative;
    path.make_preferred();

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) throw std::runtime_error("cannot create " + path.parent_path().string() + ": " +
                                     ec.message());

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << content;
    out.close();
    if (!out) throw std::runtime_error("cannot finish writing " + path.string());
    return path;
}

std::size_t TempTree::countFiles() const {
    std::size_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end;) {
        if (ec) break;
        std::error_code entry_ec;
        const fs::file_status status = it->symlink_status(entry_ec);
        if (!entry_ec) {
            if (fs::is_symlink(status)) {
                it.disable_recursion_pending();
            } else if (fs::is_regular_file(status)) {
                ++total;
            }
        }
        it.increment(ec);
    }
    return total;
}

std::string asciiLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string padded(int value, int width) {
    std::string text = std::to_string(value);
    if (static_cast<int>(text.size()) >= width) return text;
    return std::string(static_cast<std::size_t>(width) - text.size(), '0') + text;
}

}  // namespace hf_test
