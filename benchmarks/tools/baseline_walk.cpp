// Single-threaded std::filesystem baseline.
//
// The benchmark needs a reference that is obviously correct and obviously not
// tuned: one thread, recursive_directory_iterator, a case-insensitive substring
// match on the file name, the matching paths printed one per line. It shares no
// code with hyperfinder, so a win against it is a win against the standard
// library's straightforward approach and nothing else.
//
// Usage: baseline_walk <query> <root>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string toLowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: baseline_walk <query> <root>\n");
        return 2;
    }
#if defined(_WIN32)
    // Write the results exactly as they are, without the text-mode CRLF
    // translation: hyperfinder writes LF, so a byte-for-byte comparison of the
    // two outputs has to start from the same encoding.
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const std::string needle = toLowerAscii(argv[1]);
    const fs::path root = fs::u8path(argv[2]);

    const auto started = std::chrono::steady_clock::now();

    std::size_t matches = 0;
    std::size_t files = 0;
    std::size_t directories = 0;
    std::string output;

    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (!ec) {
        const fs::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const fs::directory_entry& entry = *it;
            const std::string name = entry.path().filename().u8string();

            // Same rule as the tool: a leading dot is hidden, and hidden
            // entries are not reported. This keeps the comparison honest about
            // how much work each side actually does.
            if (!name.empty() && name.front() == '.') {
                if (entry.is_directory(ec)) it.disable_recursion_pending();
                continue;
            }

            std::error_code type_ec;
            if (entry.is_directory(type_ec)) {
                ++directories;
                continue;
            }
            if (!entry.is_regular_file(type_ec)) continue;

            ++files;
            if (toLowerAscii(name).find(needle) != std::string::npos) {
                ++matches;
                output += entry.path().u8string();
                output += '\n';
            }
        }
    }

    const double elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    std::fwrite(output.data(), 1, output.size(), stdout);
    std::fprintf(stderr, "%zu matches, %zu files, %zu directories scanned in %.1f ms\n", matches,
                 files, directories, elapsed);
    return matches == 0 ? 1 : 0;
}
