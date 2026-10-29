#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include <hyperfinder/file_searcher.h>

namespace {

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " <search_term> <directory_path>\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    try {
        const std::string searchTerm = argv[1];
        const fs::path searchPath = argv[2];

        std::cout << "Scanning " << searchPath.string() << " for \"" << searchTerm << "\"...\n";

        FileSearcher searcher(searchTerm, searchPath);

        // search() returns only when every worker has finished, so this
        // interval covers the whole scan. steady_clock cannot jump backwards.
        const auto start = std::chrono::steady_clock::now();
        searcher.search();
        const auto finish = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed = finish - start;

        searcher.displayResults();

        const SearchStats stats = searcher.stats();
        std::cout << "\n----------------------------------------\n";
        std::cout << "Scan completed in " << elapsed.count() << " seconds\n";
        std::cout << "Files scanned: " << stats.files_scanned << "\n";
        std::cout << "Directories scanned: " << stats.directories_scanned << "\n";
        std::cout << "Paths skipped: " << stats.paths_skipped << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
