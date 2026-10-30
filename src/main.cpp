#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include <hyperfinder/cli.h>
#include <hyperfinder/file_searcher.h>

#include "output.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

int run(const std::vector<std::string>& args) {
    hyperfinder::prepareConsole();

    const hyperfinder::ParseResult parsed = hyperfinder::parseArguments(args);
    if (!parsed.should_run) {
        // Help and version go to stdout, usage errors to stderr; parsing
        // already decided which buffer is filled.
        std::cout << parsed.stdout_text;
        std::cout.flush();
        std::cerr << parsed.stderr_text;
        std::cerr.flush();
        return parsed.exit_code;
    }

    try {
        hyperfinder::FileSearcher searcher(parsed.options);
        searcher.search();

        const hyperfinder::SearchStats stats = searcher.stats();
        const std::size_t count = searcher.results().size();

        if (parsed.options.count_only) {
            std::cout << count << '\n';
            std::cout.flush();
        } else {
            hyperfinder::writeMatches(searcher.results(), parsed.options,
                                      hyperfinder::resolveColor(parsed.options.color));
        }

        hyperfinder::writeSummary(stats, count);

        // 1 means "the search ran and found nothing", which is not an error.
        return count == 0 ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

}  // namespace

#if defined(_WIN32)

namespace {

// Windows hands the process UTF-16, and the narrow argv the C runtime builds is
// encoded in the ANSI code page, which cannot represent a Persian name. Take
// the wide command line and convert it to UTF-8 instead, so every argument
// survives, and so the rest of the program only ever deals with UTF-8.
std::string wideToUtf8(const wchar_t* text) {
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};

    std::string result(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(length - 1));  // drop the terminator
    return result;
}

}  // namespace

int main() {
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    std::vector<std::string> args;
    if (argv != nullptr) {
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) args.push_back(wideToUtf8(argv[i]));
        ::LocalFree(argv);
    }
    if (args.empty()) args.emplace_back("hyperfinder");

    return run(args);
}

#else

int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv, argv + argc));
}

#endif
