#!/usr/bin/env bash
#
# Convert the rendered screenshots to PNGs for the README.
#
# The PPMs come from core/test/render_docs, which drives the real screen
# renderers at the real 480x480 with the real fonts. Regenerate both after any
# layout change:
#
#   cd core/test && make render_docs && ./render_docs && ../../docs/img/build.sh
#
# Needs ImageMagick. The PPMs are 16-bit and are converted down to 8-bit here:
# many viewers, macOS Preview among them, will not open a 16-bit PNG.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../core/test"

shopt -s nullglob
shots=("$SRC"/shot_*.ppm)
if [ ${#shots[@]} -eq 0 ]; then
    echo "no screenshots found; run core/test/render_docs first" >&2
    exit 1
fi

for f in "${shots[@]}"; do
    name="$(basename "$f" .ppm)"
    name="${name#shot_}"
    magick "$f" -depth 8 PNG24:"$HERE/$name.png"
    echo "  $name.png"
done

echo "wrote ${#shots[@]} images to docs/img/"
