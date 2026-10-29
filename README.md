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
  new task. A task never blocks on the tasks it spawns, so the pool cannot
  deadlock even with a single worker.
- **Completion tracking**: the pool counts queued and running tasks, and
  `waitIdle()` returns only when the whole tree has been visited.
- **Per-task result buffers**: each directory collects its own matches and
  merges them once, instead of locking a mutex per match.
- **Standard library only** for the tool itself: `std::filesystem`, `std::mutex`
  and `std::condition_variable`.

## Features

- [x] Multi-threaded directory traversal
- [x] Case-insensitive substring matching on file names
- [x] Worker count derived from `std::thread::hardware_concurrency()`
- [x] Deterministic output: results are sorted, repeated runs are identical
- [x] Links are never followed, so a cycle cannot make the scan run forever
- [x] Reports how many paths were skipped instead of failing silently

## Status

Work in progress. Correctness and the concurrency bugs of the original prototype
are fixed and covered by tests; the command-line interface, filtering, output
formats and benchmarks are still to come.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run it with a search term and a directory:

```bash
./build/hyperfinder <search_term> <directory_path>
```

Verified on Windows 11 with MinGW-w64 GCC 16.2.0 and CMake 4.4.4. Linux and
macOS builds are expected to work but have not been verified locally.

## Tests

The suite uses [doctest](https://github.com/doctest/doctest), fetched by CMake,
and is registered with CTest:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DHYPERFINDER_BUILD_TESTS=OFF` skips building them.

What is covered:

| Area | Checks |
|---|---|
| Correctness | results compared against a single-threaded `recursive_directory_iterator` walk on a 2560-file tree |
| Determinism | 200 consecutive searches must return identical results |
| Matching | case-insensitivity, empty directory, non-directory root |
| Tree shapes | deep tree, 3000 files in one directory |
| Links | symlink cycle, Windows junction cycle (skipped with a message when the platform refuses to create the link) |
| Thread pool | `waitIdle`, nested enqueue, single-worker nesting, future values, exception propagation, destruction while idle and with pending work, zero threads |

Fixtures are created under the system temp directory and removed when the test
ends.

### Sanitizers

`-DHYPERFINDER_SANITIZER=address|thread|undefined|address,undefined` builds the
whole project with the sanitizer flags:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DHYPERFINDER_SANITIZER=address,undefined
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

This requires a toolchain that ships the sanitizer runtimes: GCC or Clang on
Linux or macOS. **MinGW does not**, and the option is rejected there instead of
failing at link time. No sanitizer run has been performed on Windows.

## Known limitations

- **Windows paths longer than `MAX_PATH` (260 characters)** cannot be opened
  through `std::filesystem` without a `\\?\` prefix. Deep trees are scanned up
  to the limit and the remaining paths are counted in `Paths skipped`, but files
  beyond it are not found. Long-path support is on the roadmap.
- **Links are never followed.** `--follow-symlinks` with a cycle guard is
  planned; until then a link is skipped, whether it points at a directory or a
  file.
- **No filtering yet**: no depth limit, extension filter, size or time filter,
  and no regex/glob matching. These are planned for the next phases.
- The match is a plain substring test on the file name only, not on the path and
  not on file contents.

## License

MIT - see [LICENSE](LICENSE).
