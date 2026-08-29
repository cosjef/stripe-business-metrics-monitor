#!/usr/bin/env bash
#
# Generate LVGL bitmap fonts at the spec's sizes.
#
# Face: Roboto Condensed Bold (SIL Open Font License).
#
# Spec 5.4 calls for a monospace face, on the grounds that tabular figures stop
# numbers jittering when 94 becomes 100. We tested that on hardware and chose
# differently: monospace spends a full character cell on '.', which shrinks the
# digits enough to hurt legibility at 50cm -- the property the whole device is
# built around (spec 2.2). Left alignment (spec 5.2) already prevents the
# jitter the monospace rule was protecting against, and the hero swaps whole
# screens every 8s rather than ticking digits in place.
#
# Roboto Condensed is Android's condensed system face, designed for screen
# legibility, with counters that stay open at Bold. Being condensed, it also
# buys a size step over wider faces: "$6.5k" renders at 88px here versus 64px
# in SF Compact, i.e. 10.1mm versus 7.4mm -- "across the room" rather than
# merely "glanceable" in the spec 2.2 table.
#
# Licensing matters here: an earlier iteration used SF Compact (Apple system
# font), which cannot be redistributed. Since the spec contemplates selling
# this device, the face must be one we can ship. Roboto Condensed is OFL --
# see LICENSE-RobotoCondensed.txt alongside the .ttf.
#
# The .ttf is a static wght=700 instance of Google's variable RobotoCondensed,
# vendored so this build is reproducible without network access.
#
# IMPORTANT: if the face or weight changes, regenerate the advance-width table
# in src/hero_size.c too, or sizing will disagree with what LVGL renders. See
# tools/dump_advances.py.
#
# Usage:  ./tools/gen_fonts.sh
# Output: main/fonts/*.c
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FONT="$HERE/fonts/RobotoCondensed-Bold.ttf"
OUT="$(cd "$HERE/.." && pwd)/fonts"

# Hero sizes must match hero_font_sizes[] in src/hero_size.c.
# S3 hero ladder (240x240, ~220 PPI).
HERO_SIZES=(24 32 40 52 60 64 76 88 96)

# C6 hero ladder (480x480, ~314 PPI). NOT the S3 list doubled: the C6 is 2x the
# pixels but only 1.4x the physical size, so each size is the S3 value scaled by
# the density ratio (~1.43) to keep the same millimetre height. Derived in
# geometry.c and asserted in test_layout_c6.c.
# 104 and 120 fill the coarse gap between 92 and 126: values landing there
# were losing up to 12px of type they had room for inside a card.
C6_HERO_SIZES=(35 46 58 74 86 92 104 108 120 126 137)
# Footer (18), label (20), subtitle (22).
UI_SIZES=(18 20 22)

# C6 chrome sizes, same physical derivation.
C6_UI_SIZES=(26 29 31)

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
for s in "${C6_HERO_SIZES[@]}"; do gen "$s"; done
for s in "${C6_UI_SIZES[@]}"; do gen "$s"; done

echo
echo "done. total size:"
du -sh "$OUT"
