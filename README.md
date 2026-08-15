# Stripe Revenue Display

Firmware for a single-purpose desk instrument that shows live Stripe revenue
metrics on a 1.54" 240x240 LCD. It answers one question at a time, in numbers
readable across a room, and talks to nothing except the Stripe API.

Not affiliated with Stripe.

## Status

Stages 1-3 of 8 complete: display bring-up, the full screen deck, and WiFi
provisioning. All nine screens (six rotating metrics, three device states)
render from fixture data, rotating every 8 seconds on hardware, with
tap-to-advance navigation via the onboard IMU.

On first boot the device opens a `Setup-XXXX` access point with a captive
portal that auto-opens on a phone; once provisioned it stores credentials in
NVS and joins the network. Metric values are still fixtures — live Stripe data
comes in Stages 4-6.

Tap navigation ships with a known trade-off: it reliably detects taps but
occasionally advances on its own (roughly once per 20s idle). See the build
plan for why, and why the PLUS button would be the robust fix.

See [firmware-build-plan.md](firmware-build-plan.md) for the staged plan and
progress, and [stripe-revenue-display-spec.md](stripe-revenue-display-spec.md)
for the design specification this is built from.

## Hardware

Waveshare ESP32-S3-LCD-1.54 (non-touch): ESP32-S3R8, 8MB PSRAM, 16MB flash,
ST7789 240x240 IPS panel over SPI at 40MHz.

Pin assignments are in [firmware/main/board_config.h](firmware/main/board_config.h),
taken from Waveshare's own ESP-IDF example for this board.

## Building

Requires ESP-IDF 5.4 or newer (developed against 5.5.1).

```sh
. $IDF_PATH/export.sh
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem<N> flash monitor
```

## Tests

Sizing and layout logic is pure arithmetic with no ESP-IDF dependency, so it
runs on the host:

```sh
cd firmware/test
make
```

699 checks across seven suites. Six cover pure logic (text measurement, hero
auto-sizing, baseline positioning, font coverage, palette constraints, tap
detection, IMU register decoding, and WiFi credential validation); the seventh
boots real LVGL against an
offscreen framebuffer and asserts on actual pixels — background color, ink
position, rotation dot state, and color discipline for all nine screens.

`make quick` skips the LVGL build and runs only the logic suites.

Panel bring-up itself (`display.c`: SPI, ST7789 init, backlight) is not
covered — it needs real hardware.

## Regenerating fonts

The LVGL bitmap fonts in `firmware/main/fonts/` are generated from the vendored
Roboto Condensed TTF. Regenerate them with:

```sh
cd firmware
./tools/gen_fonts.sh          # needs npx (lv_font_conv)
```

If you change the face or weight, you **must** also regenerate the glyph
advance table in `main/hero_size.c`:

```sh
./tools/dump_advances.py      # needs Pillow
```

Otherwise text sizing will silently disagree with what LVGL actually renders.

## Notable deviations from the spec

Three findings from bringing this up on real hardware contradict the written
spec. All are documented with reasoning in
[firmware-build-plan.md](firmware-build-plan.md):

1. **Background is `#000000`, not `#121211` (spec 4.1/3.1).** This panel's
   response is nearly a step function at the bottom: `0x00` is truly black,
   but `0x04` has already jumped to visible gray, and `0x04`-`0x30` collapse
   together. `#121211` renders mid-gray. `main/colortest.c` reproduces the
   measurement.

2. **Typeface is Roboto Condensed, not monospace (spec 5.4).** Monospace spends
   a full character cell on `.`, shrinking digits enough to hurt legibility at
   50cm. Roboto Condensed renders `$6.5k` at 88px (10.1mm, "across the room" in
   the spec 2.2 table) where monospace managed 60px. It also has tabular figures
   — all digits advance 505/1000 em — so it keeps the anti-jitter property that
   motivated the monospace rule in the first place.

3. **Full-buffer JSON parsing is probably viable (spec 8.3).** The spec assumes
   ~320KB of heap and mandates streaming parses; this board reports 8MB PSRAM.
   To be confirmed at Stage 5.

## License

Roboto Condensed is used under the SIL Open Font License; see
[firmware/tools/fonts/LICENSE-RobotoCondensed.txt](firmware/tools/fonts/LICENSE-RobotoCondensed.txt).
