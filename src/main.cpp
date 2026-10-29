#include <iostream>
#include <chrono>
#include "file_searcher.h"

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <search_term> <directory_path>\n";
        return 1;
    }

    std::string searchTerm = argv[1];
    std::string searchPath = argv[2];

    try
    {
        std::cout << "Scanning files...\n";
        auto start = std::chrono::high_resolution_clock::now();

        FileSearcher searcher(searchTerm, searchPath);
        searcher.search();

        // منتظر می‌مانیم تا Thread Pool کارش تمام شود (در اینجا تخریب آبجکت اتفاق می‌افتد)
        // در یک پروژه واقعی‌تر، از یک Wait Group استفاده می‌کنیم.

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;

        searcher.displayResults();

        std::cout << "\n----------------------------------------\n";
        std::cout << "Scan completed in " << elapsed.count() << " seconds\n";
        std::cout << "Files scanned: " << searcher.getScannedCount() << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}