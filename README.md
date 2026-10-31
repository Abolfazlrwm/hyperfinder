# hyperfinder

[![CI](https://github.com/Abolfazlrwm/hyperfinder/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Abolfazlrwm/hyperfinder/actions/workflows/ci.yml)

A multi-threaded command-line file finder in **C++17**.

`hyperfinder` walks a directory tree and reports every file whose name matches a
substring, spreading the traversal across a pool of worker threads instead of
walking it sequentially.

## Features

- Multi-threaded traversal with a fixed-size worker pool
- Case-insensitive matching on file names, or `--case-sensitive`
- Three matching modes: substring (default), `--glob`, and `--regex`
- Filters: `--max-depth`, `--ext`, `--exclude`, `--hidden`
- Size and age filters: `--min-size`, `--max-size`, `--newer-than`
- `--content` searches inside files and reports `path:line:text`
- `--follow-symlinks` with cycle detection (off by default)
- Deterministic, sorted output
- Machine output: `--json` (NDJSON) and `--print0`
- Clean pipes: results on stdout, summary and warnings on stderr
- Exit codes that distinguish "no match" from "could not run"
- UTF-8 paths, arguments and file contents, including on Windows
- Colour on a terminal, plain text everywhere else

## Build

| Requirement | Notes |
|---|---|
| CMake 3.21+ | `CMakePresets.json` uses preset format version 3 |
| A C++17 compiler | GCC 9+, Clang 10+ or MSVC 2019+ |
| Ninja | needed by the presets only; the plain commands accept any generator |
| Git | needed by the build, which fetches CLI11 and doctest through `FetchContent` |

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

Without presets:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`CMAKE_BUILD_TYPE` defaults to `Release` when it is not given. Options:

| Option | Default | Effect |
|---|---|---|
| `HYPERFINDER_BUILD_TESTS` | `ON` | build and register the test suite |
| `HYPERFINDER_ENABLE_SANITIZERS` | `OFF` | add the sanitizer flags |
| `HYPERFINDER_SANITIZER` | `address,undefined` | which sanitizers, when enabled |

### Install

```bash
cmake --install build/release --prefix /usr/local
```

This installs the `hyperfinder` executable, the static library, the headers
under `include/hyperfinder/`, and a CMake package config, so a downstream
project can use the library directly:

```cmake
find_package(hyperfinder REQUIRED)
target_link_libraries(app PRIVATE hyperfinder::core)
```

## Usage

```
hyperfinder [OPTIONS] [NAME-QUERY] [PATH]
```

`NAME-QUERY` is matched against the file name, not the path. `PATH` defaults to
the current directory. With `--content`, a single positional is taken as `PATH`,
because the name query is usually omitted then; two positionals are still an
optional name query followed by the path.

Given this tree:

```
demo/
├── .cache/
│   └── notes.md
├── README.md
└── src/
    ├── main.cpp
    └── main.hpp
```

searching for `main` reports both source files, and the summary goes to stderr
so the paths can be piped:

```console
$ hyperfinder main .
.\src\main.cpp
.\src\main.hpp
2 matches, 3 files, 2 directories scanned in 0.0009832 s
```

`--max-depth 0` looks at the entries directly inside the root and descends no
further. `.cache` is hidden, so it is skipped without `--hidden`:

```console
$ hyperfinder --max-depth 0 . .
.\README.md
1 match, 1 file, 1 directory scanned in 0.0004609 s
```

`--count` prints only the number:

```console
$ hyperfinder --count --ext hpp . .
1
1 match, 3 files, 2 directories scanned in 0.0006955 s
```

`--glob` and `--regex` change how the query is interpreted; they reject each
other:

```console
$ hyperfinder --glob "*.hpp" .
$ hyperfinder --regex "^main\.(cpp|hpp)$" .
```

`--content` searches inside files. A single positional is the directory to
search, and each matching line is printed as `path:line:text`:

```console
$ hyperfinder --content TODO --ext cpp src
.\src\main.cpp:42:    // TODO: replace the stub
1 match, 2 files, 2 directories scanned in 0.0011023 s
```

Size and age narrow the candidate set before a file is even opened:

```console
$ hyperfinder --min-size 10k --max-size 2M --newer-than 7d . .
```

### Options

| Option | Effect |
|---|---|
| `-j, --threads N` | worker threads; `0` (the default) means one per hardware thread |
| `-i, --ignore-case` | match case-insensitively (the default; the flag states intent) |
| `--case-sensitive` | match case-sensitively |
| `--glob` | treat the query as a glob (`*` and `?` wildcards) |
| `--regex` | treat the query as an ECMAScript regular expression |
| `--max-depth N` | directory levels to descend; `0` means the entries directly inside the root; default unlimited |
| `--ext EXT` | keep one extension, with or without the dot; repeatable |
| `--exclude GLOB` | skip entries whose name matches the glob; repeatable |
| `--hidden` | include hidden files and descend into hidden directories |
| `--min-size SIZE` | minimum size in bytes, with an optional `k`, `M` or `G` suffix (decimal) |
| `--max-size SIZE` | maximum size in bytes, same suffixes |
| `--newer-than WHEN` | modified within a window (`7d`, `12h`, `30m`, ... with `s`/`m`/`h`/`d`/`w`) or on/after a date (`YYYY-MM-DD`) |
| `--content TEXT` | only report files whose contents contain `TEXT`; prints `path:line:text` |
| `--content-max-size SIZE` | skip content search in files larger than this (default `16M`) |
| `--follow-symlinks` | follow directory links, with cycle detection |
| `--count` | print only the number of matches (matching lines with `--content`) |
| `--json` | one JSON object per line (NDJSON) |
| `--print0` | NUL-terminated records, for `xargs -0` |
| `--color WHEN` | `auto` (default), `always` or `never` |
| `--no-color` | same as `--color=never` |
| `-h, --help` | usage |
| `--version` | version |

### Output contract

- **stdout** carries matching paths, one per line, and nothing else. With
  `--count` it carries only the number; with `--content` each record is
  `path:line:text` and `--count` counts matching lines rather than files.
- `--json` writes one object per line instead:
  `{"path":"..."}` for name matches, and
  `{"path":"...","line":N,"text":"..."}` for content matches. Strings are
  escaped to valid JSON; invalid UTF-8 becomes U+FFFD.
- `--print0` terminates each record with a NUL byte instead of a newline, for
  `xargs -0`. A content record is `path:line:text`.
- `--json` and `--print0` reject each other, and neither can be combined with
  `--count`. Neither format is coloured.
- **stderr** carries the summary, warnings about skipped paths, and errors.
- Line endings are `\n` on every platform, including when stdout is redirected
  on Windows.
- **Exit codes**: `0` at least one match, `1` no match, `2` error (bad
  arguments, unreadable root). A path that cannot be read is a warning, not an
  error, so the scan still succeeds.

Colour is applied only to the matched part of a file name. `--color=auto`
enables it only when stdout is a terminal and `NO_COLOR` is unset; a pipe never
gets escape sequences. On Windows the console's virtual terminal processing is
enabled first, and a console that refuses it gets plain text instead.

### Matching rules

These are the exact rules the code implements, including the places where they
are deliberately narrower than "Unicode support" would suggest.

- **Case folding is ASCII-only.** `--ignore-case` folds `A-Z` and leaves every
  byte above `0x7F` untouched, so `Straße` and `STRASSE` do not match, and
  `CAFÉ` does not match `café`. Correct Unicode case folding needs ICU; a
  half-correct version would match the wrong names, so the limit is documented
  instead. Persian has no case, so Persian names match either way.
- **`--ext` compares the last extension only**, after normalising to a leading
  dot and lower case: `archive.tar.gz` has the extension `.gz`, and `--ext cpp`
  matches `.CPP` as well. The comparison is case-insensitive on every platform,
  like `fd`'s; on Windows the file system is case-insensitive anyway, and on
  POSIX `.CPP` and `.cpp` are distinct names that this filter treats the same.
  A dotfile such as `.gitignore` has no extension.
- **`--exclude` globs match the entry name**, not the relative path: `*` matches
  zero or more characters, `?` matches exactly one *UTF-8 code point* (so `???`
  matches the three-letter name `علی`), and everything else is literal. A
  matching directory is never entered, not merely filtered from the results.
- **`--glob` uses the same matcher as `--exclude`**, applied to the query instead
  of the exclude list.
- **`--regex` is an ECMAScript regex** (the grammar `std::regex` implements),
  matched with `regex_search`, so it is not anchored: `log` matches `build.log`
  and `^log` matches only names that start with `log`. Under the default
  case-insensitive mode the `icase` flag folds ASCII only, like the rest of the
  tool. An invalid pattern is a usage error (exit 2). RE2 was considered and not
  adopted: it would be faster and immune to backtracking, but it is a separate
  external library to fetch and link for matching short file names.
- **`--content` is a literal substring**, not a pattern, searched line by line
  and folded the same ASCII-only way. A file is skipped as binary if a NUL byte
  appears in its first 8 KiB, and files above `--content-max-size` (16 MiB by
  default) are not read. A trailing CR is stripped, so CRLF files print cleanly,
  the last line of a file without a trailing newline is still searched, and a
  line longer than 200 bytes is truncated with `...` at a UTF-8 boundary.
- **Size suffixes are decimal**: `10k` is 10,000 bytes, `5M` is 5,000,000, `1G`
  is 1,000,000,000. Both bounds are inclusive.
- **`--newer-than` accepts a window or a date.** `7d` means "modified in the last
  seven days"; `2026-01-31` means "modified on or after local midnight of that
  day". The day is checked against the real month length, so `2026-02-31` is
  rejected rather than rolled into March.
- **Hidden** means a leading dot on POSIX. On Windows it means a leading dot or
  the `FILE_ATTRIBUTE_HIDDEN` attribute, which is what Explorer's "Hidden"
  checkbox sets.
- **`--max-depth` counts directory levels.** The root is level 0, so `0` reports
  its direct entries and descends no further, `1` also enters each first-level
  subdirectory, and so on.
- **Links are not followed** unless `--follow-symlinks` is given. Following a
  link to a directory also makes symlinked *files* visible, since the target is
  what gets classified. Cycles are detected by comparing each directory's final
  path, which catches Windows junctions as well as symlinks.

## Architecture

- **Thread pool**: a fixed set of worker threads is reused for the whole scan
  instead of spawning one thread per directory.
- **Recursive task spawning**: each subdirectory found is pushed to the pool as
  a new task. A task never blocks on the tasks it spawns, so the pool cannot
  deadlock even with a single worker. The depth travels with the task; there is
  no shared depth counter.
- **Completion tracking**: the pool counts queued and running tasks, and
  `waitIdle()` returns only when the whole tree has been visited.
- **Per-task result buffers**: each directory collects its own matches and
  merges them once, instead of locking a mutex per match.
- **No printing in the library**: `FileSearcher` returns matches plus statistics
  and never touches a stream, so the search is testable without capturing
  stdout, and the command-line layer owns formatting and colour.
- **Standard library only** for the tool itself, apart from CLI11 for parsing:
  `std::filesystem`, `std::mutex` and `std::condition_variable`.

## Tests

The suite uses [doctest](https://github.com/doctest/doctest) for the library and
CMake scripts for the executable, all registered with CTest.

| Area | Checks |
|---|---|
| Correctness | results compared against a single-threaded `recursive_directory_iterator` walk on a 2560-file tree |
| Determinism | 200 consecutive searches must return identical results |
| Matching | case-insensitivity, empty directory, non-directory root |
| Tree shapes | deep tree, 3000 files in one directory |
| Links | symlink and junction cycles, followed and not followed |
| Filters | depth, extension, exclude globs, hidden files and directories |
| Matching modes | glob and regex queries, invalid regex, case sensitivity, a Persian regex |
| Size and time | min/max size, inclusive bounds, a back-dated file, relative and date cutoffs |
| Content search | line numbers, a match across a chunk boundary, CRLF, binary skip, long-line truncation, UTF-8 |
| Output formats | JSON escaping (control chars, invalid UTF-8) and a byte-level `--print0` check |
| Glob matcher | `*`, `?`, literals, empty pattern, `a*a*a*a*a*b`, UTF-8 code points |
| UTF-8 | Persian, accented and emoji names, end to end |
| Thread pool | `waitIdle`, nested enqueue, single-worker nesting, future values, exception propagation, destruction while idle and with pending work |
| Command line | every option, usage errors, `--help`, `--version` |
| Executable | exit codes, stream separation, `--count` and the filters against a fixed tree |

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

**MinGW does not ship the sanitizer runtimes** (no `libasan`, `libubsan` or
`libtsan`), and the option is rejected there instead of failing at link time.
Sanitizer runs happen on Linux in CI; they have never been run on Windows.

## Verification

Every number and claim on this page comes from a command that was actually run.
Unless stated otherwise the machine is **Windows 11, MinGW-w64 GCC 16.2.0,
CMake 4.4.4, Ninja**.

- `cmake --preset dev`, `cmake --build --preset dev` and `ctest --preset dev`
  build with zero warnings and pass: **91 doctest cases / 265 assertions**, plus
  **18 end-to-end cases** that run the built executable.
- The install rules were checked earlier by installing into a temporary prefix
  and building a separate consumer project against it with `find_package`.
- **Linux (GCC and Clang), macOS (AppleClang), Windows (MSVC), plus ASan+UBSan
  and TSan**: run by CI on every push, see
  [`.github/workflows/ci.yml`](.github/workflows/ci.yml). None of those has been
  run locally.

## Known limitations

- **Windows paths longer than `MAX_PATH` (260 characters)** cannot be opened
  through `std::filesystem` without a `\\?\` prefix. Deep trees are scanned up
  to the limit and the remaining paths are counted in the summary, but files
  beyond it are not found. Long-path support is on the roadmap.
- **Windows scans are slower than Linux ones.** The reparse-point check that
  keeps junctions from being followed costs one `GetFileAttributesW` per entry.
  A `FindFirstFileExW`-based walk is planned for the performance phase.
- **Case folding is ASCII-only** (see above), for file names and for
  `--content` alike: `Straße`/`STRASSE` and `CAFÉ`/`café` do not match.
- **`.gitignore` rules are not applied.** This is a deliberate decision, not an
  oversight; see the note below.
- **`--exclude` matches names, not paths**, so `--exclude build/*` does not mean
  what it does in a shell; exclude the directory name instead.
- Following a link makes the same file reachable under two names, and both are
  reported; only directory cycles are suppressed.

### Why `.gitignore` is not supported yet

Tools like `fd` and `ripgrep` skip paths that a repository's `.gitignore` rules
exclude, and that is genuinely useful. Implementing it correctly is harder than
it looks: the full semantics include negation (`!pattern`), anchoring (`/`),
directory-only patterns (trailing `/`), nested `.gitignore` files that apply only
below their own directory, precedence between files, `**` globs, and the global
and per-repository excludes in `.git/config`. A partial implementation that
handles the common cases but silently gets precedence or negation wrong is worse
than none: it hides files a user expects to find and gives no indication why.

So `hyperfinder` does not read `.gitignore` yet. Until it does, the explicit
`--exclude` flag is the supported way to prune a tree, and it does exactly what
it says. A correct `.gitignore` implementation is on the roadmap; a half-correct
one is not.

## License

MIT - see [LICENSE](LICENSE).
