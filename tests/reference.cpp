#include "reference.hpp"

#include "temp_tree.hpp"

#include <algorithm>
#include <sstream>

namespace hf_test {

std::vector<std::string> referenceMatches(const std::filesystem::path& root,
                                          const std::string& needle) {
    namespace fs = std::filesystem;

    const std::string lowered = asciiLower(needle);
    std::vector<std::string> found;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                             ec),
         end;
         it != end;) {
        if (ec) break;

        std::error_code entry_ec;
        const fs::file_status status = it->symlink_status(entry_ec);
        if (!entry_ec) {
            if (fs::is_symlink(status)) {
                // recursive_directory_iterator follows directory symlinks by
                // default; the searcher never does.
                it.disable_recursion_pending();
            } else if (fs::is_regular_file(status)) {
                // u8string, not string: the fixture may contain Persian, accented
                // or emoji names, and string() converts through the ANSI code page
                // on Windows, which either throws or mangles them.
                const std::string name = asciiLower(it->path().filename().u8string());
                if (name.find(lowered) != std::string::npos) {
                    found.push_back(it->path().u8string());
                }
            }
        }

        it.increment(ec);
    }

    std::sort(found.begin(), found.end());
    return found;
}

std::string describeDifference(const std::vector<std::string>& actual,
                               const std::vector<std::string>& expected) {
    if (actual == expected) return {};

    std::ostringstream out;
    out << "actual size " << actual.size() << ", expected " << expected.size();
    const std::size_t common = std::min(actual.size(), expected.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (actual[i] != expected[i]) {
            out << "; first difference at index " << i << ": actual '" << actual[i]
                << "', expected '" << expected[i] << "'";
            return out.str();
        }
    }
    if (actual.size() > common) {
        out << "; first extra entry: '" << actual[common] << "'";
    } else if (expected.size() > common) {
        out << "; first missing entry: '" << expected[common] << "'";
    }
    return out.str();
}

}  // namespace hf_test
