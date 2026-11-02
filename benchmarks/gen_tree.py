#!/usr/bin/env python3
"""Generates the synthetic directory trees the benchmarks run against.

The trees are created outside the repository (default C:/dev/bench-data) and
are always removed again by the benchmark driver, which owns the cleanup in a
try/finally. This module only knows how to build one tree and how much room it
will need, so a caller can check the disk before asking for it.

Two shapes are produced:

  wide  -- a shallow tree with many files per directory, which is the common
           case for source trees, node_modules and build output.
  deep  -- a single chain of nested directories with a few files per level,
           which stresses per-directory overhead rather than per-file work.

Nothing here touches the network and nothing outside --root is written.
"""

from __future__ import annotations

import argparse
import os
import random
import shutil
import sys
import time

# NTFS charges roughly 1 KiB of metadata for even an empty file, so the on-disk
# cost of a tree is dominated by the file count, not by the bytes written.
NTFS_BYTES_PER_FILE = 1100
# Extra headroom so a tree never lands the volume on the emergency floor.
SAFETY_BYTES = 256 * 1024 * 1024

# A deliberately mixed name population: mostly ASCII, a small slice of
# non-ASCII so the UTF-8 paths stay exercised, and dot-prefixed names so the
# hidden-file rule has something to skip.
EXTENSIONS = [(".dat", 60), (".log", 12), (".cpp", 10), (".md", 8), (".txt", 10)]
PERSIAN_NAMES = ["علی", "سلام", "داده", "پرونده"]
ACCENTED_NAMES = ["café", "naïve", "über", "señor"]


def _weighted_extension(rng: random.Random) -> str:
    total = sum(weight for _, weight in EXTENSIONS)
    pick = rng.randrange(total)
    for extension, weight in EXTENSIONS:
        if pick < weight:
            return extension
        pick -= weight
    return ".dat"


def _file_name(rng: random.Random, index: int) -> str:
    roll = rng.random()
    if roll < 0.002:
        stem = f"{rng.choice(PERSIAN_NAMES)}{index}"
    elif roll < 0.004:
        stem = f"{rng.choice(ACCENTED_NAMES)}{index}"
    elif roll < 0.010:
        stem = f".hidden{index}"
    else:
        stem = f"f{index:07d}"
    return stem + _weighted_extension(rng)


def _write_file(path: str, size: int, payload: bytes) -> None:
    # One open/write/close per file: this is the realistic shape of a source
    # tree and it keeps the generator itself out of the measurement.
    with open(path, "wb", buffering=0) as handle:
        handle.write(payload[:size])


def estimate_bytes(files: int) -> int:
    """Conservative upper bound for the space a tree of `files` files needs."""
    return files * NTFS_BYTES_PER_FILE + SAFETY_BYTES


def build(root: str, shape: str, files: int, seed: int = 1234,
          bytes_per_file: int = 512) -> dict:
    """Creates the tree under `root` and returns a small summary."""
    if shape not in ("wide", "deep"):
        raise ValueError(f"unknown shape: {shape}")

    rng = random.Random(seed)
    payload = bytes(rng.randrange(256) for _ in range(bytes_per_file))

    started = time.perf_counter()
    os.makedirs(root, exist_ok=False)

    directories = 0
    written = 0

    if shape == "wide":
        # About 500 files per directory: enough that a directory listing is
        # large, few enough that the tree stays shallow.
        files_per_directory = 500
        directory_count = max(1, files // files_per_directory)
        remaining = files
        for index in range(directory_count):
            take = min(files_per_directory, remaining)
            if take <= 0:
                break
            directory = os.path.join(root, f"d{index:05d}")
            os.makedirs(directory)
            directories += 1
            for _ in range(take):
                _write_file(os.path.join(directory, _file_name(rng, written)),
                            rng.randrange(0, bytes_per_file + 1), payload)
                written += 1
            remaining -= take
    else:
        # A 24-level chain. Each level is one directory holding a share of the
        # files, so the walk pays the per-directory cost 24 times and the
        # per-file cost once per file.
        levels = 24
        per_level = max(1, files // levels)
        current = root
        remaining = files
        for level in range(levels):
            take = min(per_level, remaining) if level < levels - 1 else remaining
            if level > 0:
                current = os.path.join(current, f"level{level:02d}")
                os.makedirs(current)
                directories += 1
            for _ in range(max(0, take)):
                _write_file(os.path.join(current, _file_name(rng, written)),
                            rng.randrange(0, bytes_per_file + 1), payload)
                written += 1
            remaining -= max(0, take)

    elapsed = time.perf_counter() - started
    total_bytes = 0
    for directory, _, names in os.walk(root):
        for name in names:
            try:
                total_bytes += os.path.getsize(os.path.join(directory, name))
            except OSError:
                pass

    return {
        "root": root,
        "shape": shape,
        "files": written,
        "directories": directories,
        "content_bytes": total_bytes,
        "seconds": elapsed,
    }


def remove(root: str) -> None:
    """Deletes a tree, tolerating the read-only bits Windows leaves behind."""
    shutil.rmtree(root, onerror=_force_remove)


def _force_remove(function, path, _exc_info):
    try:
        os.chmod(path, 0o700)
        function(path)
    except OSError:
        pass


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="directory to create")
    parser.add_argument("--shape", choices=("wide", "deep"), default="wide")
    parser.add_argument("--files", type=int, default=10_000)
    parser.add_argument("--seed", type=int, default=1234)
    parser.add_argument("--bytes-per-file", type=int, default=512)
    parser.add_argument("--force", action="store_true",
                        help="replace the tree if it already exists")
    arguments = parser.parse_args(argv)

    if arguments.force and os.path.isdir(arguments.root):
        remove(arguments.root)

    summary = build(arguments.root, arguments.shape, arguments.files,
                    arguments.seed, arguments.bytes_per_file)
    print(f"{summary['shape']} tree: {summary['files']} files, "
          f"{summary['directories']} directories, "
          f"{summary['content_bytes'] / 1e6:.1f} MB of content, "
          f"built in {summary['seconds']:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
