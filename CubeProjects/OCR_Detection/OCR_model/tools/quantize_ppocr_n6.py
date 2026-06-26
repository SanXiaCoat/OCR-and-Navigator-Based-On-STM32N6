#!/usr/bin/env python3
"""
PP-OCR det/rec → ONNX QDQ (INT8) for STM32N6 Neural-ART.

Usage (PC with paddle2onnx + onnxruntime):
  pip install onnx onnxruntime onnxsim pillow numpy

  # 1) Export Paddle inference dir to float ONNX (paddle2onnx), then:
  python quantize_ppocr_n6.py det \\
    --input ../raw/en_PP-OCRv4_det_infer.onnx \\
    --output ../en_PP-OCRv4_det_160_qdq.onnx \\
    --calib-dir ../calib_images \\
    --height 160 --width 160 --layout nchw

  python quantize_ppocr_n6.py rec \\
    --input ../raw/en_PP-OCRv4_rec_infer.onnx \\
    --output ../en_PP-OCRv4_rec_qdq.onnx \\
    --calib-dir ../calib_images \\
    --height 48 --width 320 --layout nchw

Then in CubeMX: Model file = *_qdq.onnx, Profile = profile_O3_force8bits, Analyze.
Pass criteria: report "implemented in software" << 259 (target < 80 det, < 50 rec).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def _preprocess_det(path: str, h: int, w: int, layout: str) -> np.ndarray:
    """PP-OCR det: ImageNet norm, NCHW float32."""
    im = Image.open(path).convert("RGB").resize((w, h), Image.BILINEAR)
    arr = np.asarray(im, dtype=np.float32) / 255.0
    mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
    std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
    arr = (arr - mean) / std
    if layout == "nchw":
        arr = arr.transpose(2, 0, 1)
    return np.expand_dims(arr, 0).astype(np.float32)


def _preprocess_rec(path: str, h: int, w: int, layout: str) -> np.ndarray:
    """PP-OCR rec: /255, NCHW float32 (no ImageNet mean)."""
    im = Image.open(path).convert("RGB").resize((w, h), Image.BILINEAR)
    arr = np.asarray(im, dtype=np.float32) / 255.0
    if layout == "nchw":
        arr = arr.transpose(2, 0, 1)
    return np.expand_dims(arr, 0).astype(np.float32)


def _list_images(calib_dir: Path) -> list[Path]:
    exts = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}
    files = sorted(p for p in calib_dir.iterdir() if p.suffix.lower() in exts)
    if not files:
        raise SystemExit(f"No images in {calib_dir}")
    return files


def _simplify_onnx(path: Path) -> None:
    try:
        import onnxsim
        import onnx
    except ImportError:
        print("skip onnxsim (not installed)", file=sys.stderr)
        return
    model = onnx.load(str(path))
    model_simp, ok = onnxsim.simplify(model)
    if ok:
        onnx.save(model_simp, str(path))
        print(f"onnxsim OK: {path}")
    else:
        print(f"onnxsim failed: {path}", file=sys.stderr)


def quantize(
    mode: str,
    input_onnx: Path,
    output_onnx: Path,
    calib_dir: Path,
    height: int,
    width: int,
    layout: str,
) -> None:
    import onnx
    from onnxruntime.quantization import (
        CalibrationMethod,
        QuantFormat,
        QuantType,
        StaticQuantConfig,
        quantize,
    )

    preprocess = _preprocess_det if mode == "det" else _preprocess_rec
    paths = _list_images(calib_dir)

    import onnxruntime as ort

    sess = ort.InferenceSession(str(input_onnx), providers=["CPUExecutionProvider"])
    input_name = sess.get_inputs()[0].name

    class _Reader:
        def __init__(self) -> None:
            self._i = 0

        def get_next(self):
            if self._i >= len(paths):
                return None
            p = paths[self._i]
            self._i += 1
            data = preprocess(str(p), height, width, layout)
            return {input_name: data}

        def rewind(self) -> None:
            self._i = 0

    output_onnx.parent.mkdir(parents=True, exist_ok=True)
    tmp = output_onnx.with_suffix(".prep.onnx")
    if input_onnx.resolve() != tmp.resolve():
        import shutil
        shutil.copy2(input_onnx, tmp)

    _simplify_onnx(tmp)

    conf = StaticQuantConfig(
        calibration_data_reader=_Reader(),
        quant_format=QuantFormat.QDQ,
        calibrate_method=CalibrationMethod.MinMax,
        activation_type=QuantType.QInt8,
        weight_type=QuantType.QInt8,
        per_channel=True,
    )
    quantize(tmp, output_onnx, conf)
    tmp.unlink(missing_ok=True)
    print(f"Wrote QDQ model: {output_onnx}")
    print("Next: CubeMX Analyze with profile_O3_force8bits, check SW epoch count.")


def main() -> None:
    ap = argparse.ArgumentParser(description="PP-OCR → QDQ ONNX for STM32N6")
    ap.add_argument("mode", choices=("det", "rec"))
    ap.add_argument("--input", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    ap.add_argument("--calib-dir", required=True, type=Path)
    ap.add_argument("--height", type=int, default=160)
    ap.add_argument("--width", type=int, default=160)
    ap.add_argument("--layout", choices=("nchw", "nhwc"), default="nchw")
    args = ap.parse_args()

    if not args.input.is_file():
        raise SystemExit(f"Missing {args.input}")
    if not args.calib_dir.is_dir():
        raise SystemExit(f"Missing calib dir {args.calib_dir}")

    quantize(
        args.mode,
        args.input,
        args.output,
        args.calib_dir,
        args.height,
        args.width,
        args.layout,
    )


if __name__ == "__main__":
    main()
