#!/usr/bin/env python3
"""Patch Appli/Core/Inc/ocr_infer.h from STEdgeAI ocr_det_c_info.json."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("c_info", type=Path, help="ocr_det_c_info.json from STEdgeAI")
    parser.add_argument(
        "--header",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "Appli/Core/Inc/ocr_infer.h",
    )
    args = parser.parse_args()

    info = json.loads(args.c_info.read_text(encoding="utf-8"))
    in_buf = next(b for b in info["buffers"] if b["name"].startswith("Input"))
    out_buf = next(b for b in info["buffers"] if not b["name"].startswith("Input"))

    _, _, h, w = in_buf["shape"]
    _, _, oh, ow = out_buf["shape"]
    in_scale = in_buf["intq"]["scales"][0]
    in_zp = in_buf["intq"]["offsets"][0]
    out_scale = out_buf["intq"]["scales"][0]
    out_zp = out_buf["intq"]["offsets"][0]

    text = args.header.read_text(encoding="utf-8")
    repl = {
        "OCR_DET_MAP_W": f"{ow}U",
        "OCR_DET_MAP_H": f"{oh}U",
        "OCR_DET_PREPROC_W": f"{w}U",
        "OCR_DET_PREPROC_H": f"{h}U",
        "OCR_DET_IN_SCALE": f"{in_scale}f",
        "OCR_DET_IN_ZP": f"({in_zp})",
        "OCR_DET_OUT_SCALE": f"{out_scale}f",
        "OCR_DET_OUT_ZP": f"({out_zp})",
    }
    for key, val in repl.items():
        text, n = re.subn(rf"(#define\s+{key}\s+)[^\n]+", rf"\g<1>{val}", text, count=1)
        if n == 0:
            raise SystemExit(f"missing macro {key} in {args.header}")

    args.header.write_text(text, encoding="utf-8")
    print(f"Updated {args.header}: det {w}x{h}, map {ow}x{oh}")


if __name__ == "__main__":
    main()
