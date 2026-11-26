# Changelog

Notable changes to hyperfinder, newest first. Within 1.x the command line and
the JSON shape are stable; breaking changes, if they ever happen, land in a
2.0 and are called out here. The CMake package uses SameMajorVersion, so a
consumer asking for 1.x never silently gets 2.x.

## [1.1.0] - 2026-11-26

### Added

- Release automation: pushing a `v*` tag builds, tests and publishes archives
  for Linux, macOS and Windows with a `SHA256SUMS` file, and cuts the release
  notes from the matching section of this file.
- A security policy pointing at GitHub's private vulnerability reporting.
- A coverage gate in CI: gcovr on Linux GCC, failing below 70% non-test line
  coverage.

### Fixed

- The colour end-to-end tests scrub `NO_COLOR` from the child environment, so
  a developer shell that exports it no longer fails three unrelated cases.
- LICENSE spells the copyright holder's full name.

### Changed

- The project version now tracks the release tags; it had stayed at 0.1.0
  since the first commit even though v1.0.0 was tagged in November.
- The Windows release binaries link the C++ runtime statically, so a machine
  with neither MinGW nor the VC++ redist runs them as they are.

## [1.0.0] - 2026-11-09

### Added

- Matching modes: `--glob` and `--regex` next to the default substring search,
  with the matched span coloured in all three modes.
- Size filters `--min-size` / `--max-size` (decimal `k`/`M`/`G` suffixes,
  inclusive bounds) and `--newer-than`, which accepts a relative window
  (`7d`, `12h`, `30m`, ...) or an ISO date (`YYYY-MM-DD`).
- `--content`: literal text search inside files, printed as `path:line:text`,
  with a binary-skip probe and a `--content-max-size` cap (default 16 MiB).
- Machine output: `--json` (one NDJSON object per line) and `--print0`
  (NUL-terminated records), plus `--count` for a bare number.
- Windows: the walk enumerates with `FindFirstFileExW` and folds file names in
  place, which drops the per-entry `GetFileAttributesW` and one heap
  allocation per name; the numbers are in `benchmarks/PROFILE.md`.
- CONTRIBUTING, a code of conduct, issue templates, a pull request template,
  and dependabot config.

### Fixed

- `--ext` and `--exclude` no longer swallow the positional arguments that
  follow them.
- `--newer-than` compares whole seconds instead of nanoseconds, so a
  file_time_type that cannot represent the cutoff cannot push a matching file
  past it.
- Colouring the matched span in glob and regex mode: the pattern describes the
  whole name, so highlighting the literal query painted nothing.

## [0.1.0] - 2026-10-29

Initial release. A multi-threaded tree walk with a fixed worker pool and
per-task result buffers, substring name matching, `--max-depth`, `--ext`,
`--exclude` and `--hidden` filters, a UTF-8-aware glob matcher, a CLI11 front
end, sorted deterministic output with results on stdout and the summary on
stderr, and the 0/1/2 exit codes. Ships with CMake presets (dev, release,
asan, tsan), install rules, a CMake package config, and a doctest suite that
CI runs on Linux (GCC, Clang), macOS and MSVC, with sanitizer jobs on top.
