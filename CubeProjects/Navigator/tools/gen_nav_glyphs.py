#!/usr/bin/env python3
"""Generate nav_glyphs_builtin.c from system Chinese font (16x16 ALIENTEK format)."""

import os
from PIL import Image, ImageDraw, ImageFont

OUT_C = os.path.join(
    os.path.dirname(__file__),
    "..",
    "STM32CubeIDE",
    "Appli",
    "Application",
    "User",
    "Core",
    "nav_glyphs_builtin.c",
)
OUT_H = os.path.join(os.path.dirname(OUT_C), "nav_glyphs_builtin.h")

CHARS = {
    # static labels
    "当": 0x5F53,
    "前": 0x524D,
    "位": 0x4F4D,
    "置": 0x7F6E,
    "：": 0xFF1A,
    "即": 0x5373,
    "将": 0x5C06,
    "进": 0x8FDB,
    "入": 0x5165,
    "到": 0x5230,
    "下": 0x4E0B,
    "一": 0x4E00,
    "步": 0x6B65,
    "的": 0x7684,
    "距": 0x8DDD,
    "离": 0x79BB,
    "剩": 0x5269,
    "余": 0x4F59,
    "时": 0x65F6,
    "间": 0x95F4,
    # navigation common
    "米": 0x7C73,
    "后": 0x540E,
    "左": 0x5DE6,
    "右": 0x53F3,
    "转": 0x8F6C,
    "道": 0x9053,
    "路": 0x8DEF,
    "直": 0x76F4,
    "行": 0x884C,
    "公": 0x516C,
    "里": 0x91CC,
    "百": 0x767E,
    "千": 0x5343,
    "万": 0x4E07,
    "大": 0x5927,
    "小": 0x5C0F,
    "向": 0x5411,
    "出": 0x51FA,
    "口": 0x53E3,
    "高": 0x9AD8,
    "速": 0x901F,
    "环": 0x73AF,
    "岛": 0x5C9B,
    "调": 0x8C03,
    "头": 0x5934,
    "沿": 0x6CBF,
    "驶": 0x9A76,
    "人": 0x4EBA,
    "民": 0x6C11,
    "建": 0x5EFA,
    "设": 0x8BBE,
    "街": 0x8857,
    "区": 0x533A,
    "桥": 0x6865,
    "东": 0x4E1C,
    "南": 0x5357,
    "西": 0x897F,
    "北": 0x5317,
    "中": 0x4E2D,
    "国": 0x56FD,
    "收": 0x6536,
    "费": 0x8D39,
    "站": 0x7AD9,
    "服": 0x670D,
    "务": 0x52A1,
}


def pil_to_alientek(img: Image.Image) -> bytes:
    """Convert 16x16 L image to ALIENTEK GBK16 column bitmap (32 bytes)."""
    px = img.load()
    out = bytearray(32)
    idx = 0
    for cx in range(16):
        for byte_i in range(2):
            b = 0
            for bit in range(8):
                y = byte_i * 8 + bit
                v = px[cx, y]
                if v < 128:
                    b |= 0x80 >> bit
            out[idx] = b
            idx += 1
    return bytes(out)


def load_font():
    for fp in (
        r"C:/Windows/Fonts/simsun.ttc",
        r"C:/Windows/Fonts/msyh.ttc",
        r"C:/Windows/Fonts/simhei.ttf",
    ):
        if os.path.exists(fp):
            return ImageFont.truetype(fp, 14)
    raise SystemExit("No Chinese system font found")


def main():
    font = load_font()
    count = len(CHARS)

    h_lines = [
        "#ifndef NAV_GLYPHS_BUILTIN_H",
        "#define NAV_GLYPHS_BUILTIN_H",
        "",
        "#include <stdint.h>",
        "",
        "#define NAV_GLYPH16_BYTES     32U",
        f"#define NAV_BUILTIN_GLYPH_COUNT {count}U",
        "",
        "typedef struct",
        "{",
        "  uint16_t unicode;",
        "  const uint8_t mat[NAV_GLYPH16_BYTES];",
        "} nav_builtin_glyph_t;",
        "",
        "const uint8_t *nav_glyph16_builtin_get(uint16_t unicode);",
        "",
        "#endif /* NAV_GLYPHS_BUILTIN_H */",
        "",
    ]

    c_lines = [
        '/** @file nav_glyphs_builtin.c - embedded 16x16 nav glyphs (no SD lookup) */',
        "",
        "#include <stddef.h>",
        '#include "nav_glyphs_builtin.h"',
        "",
        f"static const nav_builtin_glyph_t s_nav_builtin_glyphs[NAV_BUILTIN_GLYPH_COUNT] =",
        "{",
    ]

    for ch, uni in sorted(CHARS.items(), key=lambda x: x[1]):
        img = Image.new("L", (16, 16), 255)
        draw = ImageDraw.Draw(img)
        draw.text((0, 0), ch, font=font, fill=0)
        mat = pil_to_alientek(img)
        hexbytes = ", ".join(f"0x{b:02X}" for b in mat)
        c_lines.append(f"  {{ 0x{uni:04X}U, {{ {hexbytes} }} }}, /* {ch} */")

    c_lines += [
        "};",
        "",
        "const uint8_t *nav_glyph16_builtin_get(uint16_t unicode)",
        "{",
        "  uint8_t i;",
        "  for (i = 0U; i < NAV_BUILTIN_GLYPH_COUNT; i++)",
        "  {",
        "    if (s_nav_builtin_glyphs[i].unicode == unicode)",
        "    {",
        "      return s_nav_builtin_glyphs[i].mat;",
        "    }",
        "  }",
        "  return NULL;",
        "}",
        "",
    ]

    with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h_lines))
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c_lines))

    print(f"Wrote {OUT_C} ({count} glyphs)")


if __name__ == "__main__":
    main()
