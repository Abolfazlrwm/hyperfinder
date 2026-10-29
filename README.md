# hyperfinder

A multi-threaded command-line file search tool built with **C++17**.

## Overview

hyperfinder walks a directory tree and reports every file whose name contains a
given substring. Directory traversal is spread across a pool of worker threads
instead of running sequentially.

## Architecture

- **Thread pool**: a fixed set of worker threads is reused for the whole scan
  instead of spawning one thread per directory.
- **Recursive task spawning**: each subdirectory found is pushed to the pool as a
  new task.
- **Atomic counters**: the number of scanned entries is tracked with
  `std::atomic`.
- **Standard library only**: `std::filesystem`, `std::mutex` and
  `std::condition_variable`, no third-party dependencies.

## Features

- [x] Multi-threaded directory traversal
- [x] Case-insensitive substring matching on file names
- [x] Worker count derived from `std::thread::hardware_concurrency()`
- [x] No external dependencies

## Status

Work in progress. This is the original prototype, renamed and versioned as the
starting point of a cleanup effort. Known issues are tracked in the commit
history; correctness, tests and benchmarks are not yet in place.

## Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Run it with a search term and a directory:

```bash
./hyperfinder <search_term> <directory_path>
```

Verified on Windows 11 with MinGW-w64 GCC 16.2.0 and CMake 4.2.3. Linux and
macOS builds are expected to work but have not been verified yet.

## License

MIT - see [LICENSE](LICENSE).
