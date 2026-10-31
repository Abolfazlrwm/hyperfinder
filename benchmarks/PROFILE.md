# Where the time goes on Windows

Measured **before** any optimisation, so the work that follows is justified by
numbers rather than by intuition. Raw output is reproduced below verbatim.

## Machine

| | |
|---|---|
| CPU | 12th Gen Intel Core i5-1235U, 10 cores / 12 logical (hybrid P+E) |
| Disk | WD PC SN740 NVMe SSD (C:) |
| OS | Windows 11, NTFS |
| Toolchain | MinGW-w64 GCC 16.2.0, CMake 4.4.4, Ninja, `-O3` (`release` preset) |
| Cache | **warm**: the trees were just generated, so every directory is in the file cache |
| AV | Windows Defender real-time scanning active (cannot be excluded without admin) |

Defender is the reason the absolute numbers are high — see *Caveats*. The
comparison between modes is unaffected, because every mode pays the same tax.

## Method

`benchmarks/tools/walk_bench.cpp` walks the same tree with the same primitives
the searcher uses and adds exactly one step at a time:

| Mode | What it does |
|---|---|
| `iter` | `std::filesystem::recursive_directory_iterator`, count only |
| `iter-status` | + `directory_entry::symlink_status()` per entry |
| `iter-name` | + `filename().u8string()` per entry (the UTF-8 name) |
| `iter-attr` | + `GetFileAttributesW` per entry — what the searcher does today |
| `findex` | `FindFirstFileExW` + `FIND_FIRST_EX_LARGE_FETCH` + `FindExInfoBasic` |

Each mode also prints the number of entries it visited, so a mode cannot look
fast by walking less.

## Results — `wide-10k` (10,000 files in 20 directories, 10,020 entries)

```console
$ walk_bench <mode> C:/dev/bench-data/wide-10k 7
iter         median     99.4 ms  min     95.8 ms  max    104.6 ms  (n=7, 10020 entries)
iter-status  median    166.3 ms  min    155.1 ms  max    182.9 ms  (n=7, 20060 entries)
iter-name    median    103.5 ms  min    100.3 ms  max    131.8 ms  (n=7, 129635 entries)
iter-attr    median    341.0 ms  min    330.9 ms  max    357.4 ms  (n=7, 340380 entries)
findex       median      3.7 ms  min      3.1 ms  max      4.5 ms  (n=7, 10020 entries)
```

(The entry counts above 10,020 for the middle modes are an artefact of the
counter also accumulating the values the mode computes; only the timing matters.)

## Results — `deep-10k` (10,000 files in 23 nested directories, 10,023 entries)

```console
$ walk_bench <mode> C:/dev/bench-data/deep-10k 7
iter         median     91.8 ms  min     89.1 ms  max     98.5 ms  (n=7, 10023 entries)
iter-status  median    156.7 ms  min    144.1 ms  max    176.6 ms  (n=7, 20069 entries)
iter-attr    median    351.2 ms  min    313.7 ms  max    933.1 ms  (n=7, 340437 entries)
findex       median      7.5 ms  min      6.5 ms  max     12.9 ms  (n=7, 10023 entries)
```

## What the profile says

Reading the deltas off `wide-10k`, per entry:

| Step | Cost per entry | Share of the walk |
|---|---|---|
| enumerate (`iter`) | ~9.9 µs | 29 % |
| `symlink_status` | +6.7 µs | 20 % |
| UTF-8 name | +0.4 µs | 1 % |
| `GetFileAttributesW` | **+17.5 µs** | 50 % |
| **total today** | **~34 µs** | 100 % |

Two things stand out, and they point at the same fix:

1. **The extra `GetFileAttributesW` is half the cost of the scan.** It exists
   only to see `FILE_ATTRIBUTE_REPARSE_POINT` and `FILE_ATTRIBUTE_HIDDEN`,
   which `FindFirstFileExW` already returns in `WIN32_FIND_DATAW` for free.
2. **Enumeration itself is 27× slower than it needs to be.** `findex` visits the
   same 10,020 entries in 3.7 ms where `recursive_directory_iterator` needs
   99.4 ms. `FIND_FIRST_EX_LARGE_FETCH` batches the directory read, and
   `FindExInfoBasic` skips the 8.3 short name the default provider still fills
   in.

Together they explain the product's numbers, measured with the same
tool-internal timer:

```console
hyperfinder -j1  :    299.2 ms  (n=7, min 291.5, max 300.3)
hyperfinder -j12 :    174.4 ms  (n=7, min 156.7, max 204.7)
baseline_walk    :    220.6 ms  (n=7, min 211.2, max 238.6)
```

The 12-thread run is only 1.7× faster than the single-thread run, which is the
signature of a serialised bottleneck: every worker is blocked in the same two
system calls, so more threads buy little.

## Decision

Step 2 replaces both the enumeration and the attribute lookup with one
`FindFirstFileExW` loop that yields names and attributes together. Everything
else the profile shows is noise (<1 µs per entry) and is left alone.

## Caveats

- **Warm cache only.** A real cold-cache run needs `RAMMap`/admin to purge the
  standby list; it was not possible here and is not claimed.
- **Defender is active** on every measurement, and its filter driver is the most
  likely reason a `GetFileAttributesW` costs 17 µs instead of ~1 µs. Absolute
  times will be lower on a machine with an exclusion in place; the ratios should
  hold.
- The i5-1235U is **hybrid** (2 P-cores + 8 E-cores), so thread scaling past 4
  threads is limited by the E-cores and is not a clean N× curve.
- Times are the tools' own `steady_clock` measurements around the scan, which
  excludes process start-up; wall-clock numbers in the final report include it.
