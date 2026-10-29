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

| Requirement | Notes |
|---|---|
| CMake 3.21+ | `CMakePresets.json` uses preset format version 3 |
| A C++17 compiler | GCC 9+, Clang 10+ or MSVC 2019+ |
| Ninja | needed by the presets only; the plain commands accept any generator |
| Git | needed by the tests only, which fetch doctest through `FetchContent` |

### With presets

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

| Preset | Build type | Purpose |
|---|---|---|
| `dev` | Debug | unoptimised, with the test suite |
| `release` | Release | optimised, with the test suite |
| `asan` | Debug | AddressSanitizer + UndefinedBehaviorSanitizer |
| `tsan` | Debug | ThreadSanitizer |

Each preset configures into `build/<preset>`, so they can coexist. The compiler
is deliberately not pinned: whatever `g++`/`clang++` comes first on `PATH` is
used. On Windows, add the WinLibs `bin` directory to `PATH` and
`cmake --preset release` picks up its GCC and its Ninja.

The sanitizer presets need a toolchain that ships the runtimes, meaning GCC or
Clang on Linux or macOS. On MinGW they stop during configure with an explanation
instead of failing later at link time.

### Without presets

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`CMAKE_BUILD_TYPE` defaults to `Release` when it is not given. The options are:

| Option | Default | Effect |
|---|---|---|
| `HYPERFINDER_BUILD_TESTS` | `ON` | build and register the test suite |
| `HYPERFINDER_ENABLE_SANITIZERS` | `OFF` | add the sanitizer flags |
| `HYPERFINDER_SANITIZER` | `address,undefined` | which sanitizers, when enabled |

Run it with a search term and a directory:

```bash
./build/release/hyperfinder <search_term> <directory_path>
```

### Install

```bash
cmake --install build/release --prefix /usr/local
```

This installs the `hyperfinder` executable, the static library, the headers
under `include/hyperfinder/`, and a CMake package config. A downstream project
can then use the library directly:

```cmake
find_package(hyperfinder REQUIRED)
target_link_libraries(app PRIVATE hyperfinder::core)
```

## Verification

Every number and claim on this page comes from a command that was actually run.

- **Windows 11, MinGW-w64 GCC 16.2.0, CMake 4.4.4**: `cmake --preset dev` and
  `cmake --preset release` build with zero warnings and pass the full suite. The
  install rules were checked by installing into a temporary prefix and building
  a separate consumer project against it with `find_package`.
- **Linux (GCC and Clang), macOS (AppleClang), Windows (MSVC), plus ASan+UBSan
  and TSan**: verified by CI on every push, see
  [`.github/workflows/ci.yml`](.github/workflows/ci.yml). None of those has been
  run locally.

## Tests

The suite uses [doctest](https://github.com/doctest/doctest), fetched by CMake,
and is registered with CTest:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
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

The `asan` and `tsan` presets build the whole project with the sanitizer flags
and export `ASAN_OPTIONS`, `UBSAN_OPTIONS` and `TSAN_OPTIONS` for the test run so
that a finding aborts it instead of being buried in the log:

```bash
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The equivalent without presets:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DHYPERFINDER_ENABLE_SANITIZERS=ON -DHYPERFINDER_SANITIZER=address,undefined
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

**MinGW does not ship the sanitizer runtimes** (no `libasan`, `libubsan` or
`libtsan`), and the option is rejected there instead of failing at link time.
Sanitizer runs happen on Linux in CI; they have never been run on Windows.

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
