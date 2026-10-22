#!/usr/bin/env python3
"""Repacks the project's data/ folder into data.pak, written next to this script.

Layout: <project>/data/ is packed, and <project>/tools/data_repack.py writes
<project>/tools/data.pak. The format matches tools/pack_data.cpp and src/pak.cpp:

    magic      8 bytes  "RTSPAK01"
    count      uint32   little-endian
    per file:  uint32 name length, name bytes ("data/..." with forward slashes),
               uint64 offset, uint64 size
    contents   the file bytes, in the same order as the table
"""

import struct
import sys
from pathlib import Path

MAGIC = b"RTSPAK01"

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent
DATA_DIR = PROJECT_DIR / "data"
OUTPUT = TOOLS_DIR / "data.pak"


def collect_files(root: Path):
    """Every regular file under root, as (pak name, path on disk), sorted by name."""
    prefix = root.name  # "data"
    entries = []
    for path in root.rglob("*"):
        if path.is_file():
            name = f"{prefix}/{path.relative_to(root).as_posix()}"
            entries.append((name, path))
    # Sort by UTF-8 bytes, which is the order pack_data.cpp uses
    entries.sort(key=lambda e: e[0].encode("utf-8"))
    return entries


def build_pak(entries) -> bytes:
    # The header comes first, so the content offsets depend on its size
    header_size = len(MAGIC) + 4
    for name, _ in entries:
        header_size += 4 + len(name.encode("utf-8")) + 8 + 8

    table = bytearray()
    contents = bytearray()
    offset = header_size
    for name, path in entries:
        data = path.read_bytes()
        name_bytes = name.encode("utf-8")
        table += struct.pack("<I", len(name_bytes))
        table += name_bytes
        table += struct.pack("<QQ", offset, len(data))
        contents += data
        offset += len(data)

    return MAGIC + struct.pack("<I", len(entries)) + bytes(table) + bytes(contents)


def main() -> int:
    if not DATA_DIR.is_dir():
        print(f"data_repack: no data folder at {DATA_DIR}", file=sys.stderr)
        return 1

    entries = collect_files(DATA_DIR)
    pak = build_pak(entries)

    # Write to a temporary file first, so a failed run never leaves a half-written data.pak
    tmp = OUTPUT.with_suffix(".pak.tmp")
    tmp.write_bytes(pak)
    tmp.replace(OUTPUT)

    print(f"data_repack: wrote {len(entries)} files to {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
