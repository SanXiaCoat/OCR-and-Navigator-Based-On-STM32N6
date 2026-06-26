#!/usr/bin/env python3
"""Generate Appli/Core/Inc/ocr_rec_dict.h from PaddleOCR en_dict.txt."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--dict",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "资料/onnx_run_on_mac/en_dict.txt",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "Appli/Core/Inc/ocr_rec_dict.h",
    )
    args = parser.parse_args()

    chars: list[str] = []
    for line in args.dict.read_text(encoding="utf-8").splitlines():
        chars.append(line.rstrip("\r\n"))
    chars.append(" ")

    out = args.output
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "#ifndef OCR_REC_DICT_H",
        "#define OCR_REC_DICT_H",
        "",
        f"#define OCR_REC_DICT_SIZE {len(chars) + 1}",
        "",
        "static const char *const ocr_rec_dict[OCR_REC_DICT_SIZE] = {",
        '  "blank",',
    ]
    for ch in chars:
        esc = ch.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'  "{esc}",')
    lines += ["};", "", "#endif /* OCR_REC_DICT_H */", ""]
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out} ({len(chars) + 1} entries)")


if __name__ == "__main__":
    main()
