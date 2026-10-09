#!/usr/bin/env python3
"""Merge shader pipeline cache directories into one destination.

Copies .spv / .meta / .key / .bin blobs from each source into dest. An existing
file of the same name is left in place (first source wins). Intended for
combining BB_CACHE_COLLECT dumps into cache_bundled/<family>/<serial>/.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

EXTS = {".spv", ".meta", ".key", ".bin"}


def merge_one(src: Path, dest: Path) -> tuple[int, int]:
    copied = 0
    skipped = 0
    if not src.is_dir():
        raise SystemExit(f"source is not a directory: {src}")
    dest.mkdir(parents=True, exist_ok=True)
    for file in src.rglob("*"):
        if not file.is_file() or file.suffix.lower() not in EXTS:
            continue
        out = dest / file.relative_to(src)
        if out.exists():
            skipped += 1
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, out)
        copied += 1
    return copied, skipped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dest", type=Path, help="output cache directory")
    parser.add_argument("sources", nargs="+", type=Path, help="input cache directories")
    args = parser.parse_args()
    total_copied = 0
    total_skipped = 0
    for source in args.sources:
        copied, skipped = merge_one(source, args.dest)
        total_copied += copied
        total_skipped += skipped
        print(f"{source}: copied {copied}, skipped {skipped}")
    print(f"merged {len(args.sources)} sources into {args.dest} "
          f"({total_copied} new, {total_skipped} existing)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
