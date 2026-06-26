#!/usr/bin/env python3
"""Pack ATK SYSTEM/FONT binaries into one image for XSPI2 @ 0x71200000."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

FONT_PACK_MAGIC = 0x46545046  # 'FTPF'
FONT_PACK_VERSION = 1
FONT_FILES = (
    "UNIGBK.BIN",
    "GBK12.FON",
    "GBK16.FON",
    "GBK24.FON",
    "GBK32.FON",
)


def pack_font_dir(font_dir: Path, output: Path) -> None:
    blobs: list[bytes] = []
    sizes: list[int] = []

    for name in FONT_FILES:
        path = font_dir / name
        if not path.is_file():
            raise FileNotFoundError(f"missing font file: {path}")
        data = path.read_bytes()
        blobs.append(data)
        sizes.append(len(data))
        print(f"  {name}: {len(data)} bytes")

    header = struct.pack(
        "<6I",
        FONT_PACK_MAGIC,
        FONT_PACK_VERSION,
        sizes[0],
        sizes[1],
        sizes[2],
        sizes[3],
    )
    header += struct.pack("<I", sizes[4])

    payload = header + b"".join(blobs)
    output.write_bytes(payload)
    print(f"Wrote {output} ({len(payload)} bytes)")
    print("Flash to XSPI2 address 0x71200000 with STM32CubeProgrammer")
    print("(OCR det weights occupy 0x71000000; do not overlap)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "font_dir",
        type=Path,
        help="Directory containing UNIGBK.BIN and GBK*.FON (e.g. ATK SYSTEM/FONT)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("font_pack.bin"),
        help="Output pack file (default: font_pack.bin)",
    )
    args = parser.parse_args()

    if not args.font_dir.is_dir():
        print(f"Not a directory: {args.font_dir}", file=sys.stderr)
        return 1

    pack_font_dir(args.font_dir, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
