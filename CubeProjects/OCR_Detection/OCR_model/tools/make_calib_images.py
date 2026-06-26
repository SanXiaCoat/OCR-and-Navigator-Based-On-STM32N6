#!/usr/bin/env python3
"""Create placeholder calibration images for QDQ quantization."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parents[1] / "calib_images"
TEXTS = [
    "Hello World",
    "OCR Test 123",
    "ACKNOWLEDGEMENTS",
    "Python ONNX PP-OCRv4",
    "STM32N647 Edge AI",
    "ABCDEFG abcdefg 0123456789",
    "Do not stand sit climb",
    "Sample document line",
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for i, text in enumerate(TEXTS):
        im = Image.new("RGB", (640, 480), (245, 245, 245))
        draw = ImageDraw.Draw(im)
        draw.rectangle((40, 180, 600, 280), outline=(0, 0, 0), width=2)
        draw.text((60, 210), text, fill=(0, 0, 0))
        im.save(OUT / f"calib_{i:02d}.png")
    print(f"Wrote {len(TEXTS)} images to {OUT}")


if __name__ == "__main__":
    main()
