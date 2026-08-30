#!/usr/bin/env python3
"""
Print the per-glyph advance-width table for core/src/hero_size.c.

The firmware measures text width per glyph (see hero_size.h for why it does not
use the spec's 0.6em monospace constant). That table must match the font that
LVGL actually renders, so regenerate it whenever the face or weight changes:

    pip install Pillow
    ./tools/dump_advances.py > /tmp/table.txt

then paste the array body into glyph_adv_x1000[] in core/src/hero_size.c and update
GLYPH_FALLBACK_X1000 to the reported MAX.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT = os.path.join(os.path.dirname(__file__), "fonts", "RobotoCondensed-Bold.ttf")

# Must match RANGE in gen_fonts.sh and GLYPH_FIRST/GLYPH_LAST in hero_size.c.
FIRST, LAST = 0x20, 0x7A

# Measure at 1000px so raw advances are already in thousandths of an em.
EM = 1000


def main() -> int:
    if not os.path.exists(FONT):
        print(f"error: font not found at {FONT}", file=sys.stderr)
        return 1

    draw = ImageDraw.Draw(Image.new("RGB", (10, 10)))
    font = ImageFont.truetype(FONT, EM)

    advances = [round(draw.textlength(chr(cp), font=font))
                for cp in range(FIRST, LAST + 1)]

    print(f"/* {os.path.basename(FONT)}, glyphs 0x{FIRST:02X}-0x{LAST:02X} */")
    line = "    "
    for i, adv in enumerate(advances):
        line += f"{adv:4d},"
        if (i + 1) % 16 == 0:
            print(line)
            line = "    "
    if line.strip():
        print(line)

    print(f"\n/* GLYPH_FALLBACK_X1000 should be {max(advances)} */")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
