#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include <hyperfinder/cli.h>
#include <hyperfinder/file_searcher.h>

int main(int argc, char* argv[]) {
    const std::vector<std::string> args(argv, argv + argc);
    const hyperfinder::ParseResult parsed = hyperfinder::parseArguments(args);

    if (!parsed.should_run) {
        std::cout << parsed.stdout_text;
        std::cerr << parsed.stderr_text;
        return parsed.exit_code;
    }

    try {
        hyperfinder::FileSearcher searcher(parsed.options);

        // search() returns only when every worker has finished, so this
        // interval covers the whole scan. steady_clock cannot jump backwards.
        const auto start = std::chrono::steady_clock::now();
        searcher.search();
        const auto finish = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = finish - start;

        searcher.displayResults();

        const hyperfinder::SearchStats stats = searcher.stats();
        std::cout << "\n----------------------------------------\n";
        std::cout << "Scan completed in " << elapsed.count() << " seconds\n";
        std::cout << "Files scanned: " << stats.files_scanned << "\n";
        std::cout << "Directories scanned: " << stats.directories_scanned << "\n";
        std::cout << "Paths skipped: " << stats.paths_skipped << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
