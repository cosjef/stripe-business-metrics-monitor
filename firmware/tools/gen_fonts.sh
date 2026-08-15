#!/usr/bin/env bash
#
# Generate LVGL bitmap fonts at the spec's sizes.
#
# Face: SF Compact Bold.
#
# Spec 5.4 calls for a monospace face, on the grounds that tabular figures stop
# numbers jittering when 94 becomes 100. We tested that on hardware and chose
# differently: monospace spends a full character cell on '.', which shrinks the
# digits enough to hurt legibility at 50cm -- the property the whole device is
# built around (spec 2.2). Left alignment (spec 5.2) already prevents the
# jitter the monospace rule was protecting against, and the hero swaps whole
# screens every 8s rather than ticking digits in place.
#
# SF Compact is Apple's small-screen face; Bold survives backlight bleed on IPS
# without closing up the counters the way Heavy/Black do.
#
# The .ttf here is a static instance (wght=790) extracted from the system's
# variable SFCompact.ttf, vendored so this build is reproducible.
#
# IMPORTANT: if the face or weight changes, regenerate the advance-width table
# in main/hero_size.c too, or sizing will disagree with what LVGL renders. See
# tools/dump_advances.py.
#
# Usage:  ./tools/gen_fonts.sh
# Output: main/fonts/*.c
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FONT="$HERE/fonts/SFCompact-Bold.ttf"
OUT="$(cd "$HERE/.." && pwd)/main/fonts"

# Hero sizes must match hero_font_sizes[] in main/hero_size.c.
HERO_SIZES=(24 32 40 52 60 64 76 88 96)
# Footer (18), label (20), subtitle (22).
UI_SIZES=(18 20 22)

# Glyph range: space through 'z'. Matches the advance table in hero_size.c.
RANGE="0x20-0x7A"

mkdir -p "$OUT"

if [ ! -f "$FONT" ]; then
    echo "error: font not found at $FONT" >&2
    exit 1
fi

gen() {
    local size="$1"
    local name="stripe_sans_${size}"
    echo "  ${name}"
    # --lv-include: the generator defaults to <lvgl/lvgl.h>, but the ESP-IDF
    # lvgl component exposes the header as <lvgl.h>.
    npx -y lv_font_conv@latest \
        --font "$FONT" \
        --size "$size" \
        --bpp 4 \
        --format lvgl \
        --range "$RANGE" \
        --no-compress \
        --lv-include "lvgl.h" \
        --lv-font-name "$name" \
        -o "$OUT/${name}.c" >/dev/null
}

echo "removing previous generation..."
rm -f "$OUT"/stripe_mono_*.c "$OUT"/stripe_sans_*.c

echo "generating hero fonts..."
for s in "${HERO_SIZES[@]}"; do gen "$s"; done

echo "generating UI fonts..."
for s in "${UI_SIZES[@]}"; do gen "$s"; done

echo
echo "done. total size:"
du -sh "$OUT"
