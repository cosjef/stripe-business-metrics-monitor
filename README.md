# Stripe Revenue Display

Firmware for a single-purpose desk instrument that shows live Stripe revenue
metrics. It answers one question at a time, in numbers readable across a room,
and talks to nothing except the Stripe API.

Not affiliated with Stripe.

## Two products, one core

```
core/           portable: rendering, MRR maths, parsers, fonts, tests
firmware-s3/    Waveshare ESP32-S3-LCD-1.54  (240x240 ST7789, ESP-IDF)
firmware-c6/    Waveshare ESP32-C6 AMOLED 2.16 (480x480 CO5300, Arduino)
```

Both products compile the same `core/` sources. Neither depends on the other,
and nothing in `core/` may include `esp_*.h`, `<Arduino.h>`, `driver/*` or
`freertos/*` — that constraint is what lets the whole thing be tested on a
laptop. See [core/README.md](core/README.md).

## Status

**firmware-c6** is the active build and shows live data. It provisions itself
over a captive portal, stores credentials in NVS, fetches from Stripe over
TLS with a pinned CA, and rotates eight screens: MRR, new paid, paid subs,
cancelled, annual run rate, ARPU, net 30-day, and failed payments. Screens
with nothing to say hide themselves. Buttons and touch move the deck; the
battery shows in the label row; a failed fetch surfaces the stale screen
rather than presenting old numbers as current.

**firmware-s3** is the original ESP-IDF build for the 240x240 board. It works,
and `main` holds it. The C6 was attempted in ESP-IDF first and never drove the
panel past its first frame; [C6-HANDOFF.md](C6-HANDOFF.md) records that
failure and the bisect that could not find it.

See [stripe-revenue-display-spec.md](stripe-revenue-display-spec.md) for the
design this is built from.

## Building

**C6 (PlatformIO):**

```sh
cd firmware-c6
pio run
pio run -t upload --upload-port /dev/cu.usbmodem<N>
```

**S3 (ESP-IDF 5.4+):**

```sh
. $IDF_PATH/export.sh
cd firmware-s3
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem<N> flash monitor
```

The C6 needs no credentials at build time: it provisions over its own access
point. The serial port name changes between replugs — check
`ls /dev/cu.usbmodem*` first.

## Tests

Everything portable is tested on the host, with no hardware and no SDK:

```sh
cd core/test
make
for t in ./test_*; do [ -x "$t" ] && $t; done
```

1,591 checks across 28 suites. Most cover pure logic — text measurement, hero
auto-sizing, MRR arithmetic, the streaming JSON scanners, rotation rules,
freshness, battery thresholds, WiFi retry policy. One boots real LVGL against
an offscreen framebuffer and asserts on actual pixels.

`make quick` skips the LVGL build and runs only the logic suites. LVGL itself
arrives with the ESP-IDF component manager: `cd firmware-s3 && idf.py
reconfigure`.

Panel bring-up, the radios and the I2C peripherals are not covered — they need
real hardware, and every claim about them in the docs was checked against a
register read rather than a datasheet.

## Regenerating fonts

```sh
cd core/tools
./gen_fonts.sh            # needs npx (lv_font_conv)
```

If you change the face or weight you **must** also regenerate the glyph
advance table in `core/src/hero_size.c`:

```sh
./dump_advances.py        # needs Pillow
```

Otherwise text sizing will silently disagree with what LVGL renders.

## Notable deviations from the spec

Findings from real hardware that contradict the written spec:

1. **Background is `#000000`, not `#121211` (spec 4.1/3.1).** On the S3's IPS
   panel `0x04`-`0x30` collapse to the same mid-gray, so `#121211` renders
   grey rather than near-black. Measured with `colortest.c`. Unverified on the
   C6's AMOLED, where the reasoning does not carry over — a black pixel there
   is simply off.

2. **Typeface is Roboto Condensed, not monospace (spec 5.4).** Monospace
   spends a full character cell on `.`, shrinking digits enough to hurt
   legibility at 50cm. Roboto Condensed has tabular figures, so it keeps the
   anti-jitter property that motivated the monospace rule anyway.

3. **Streaming JSON is kept, but not for the reason the spec gives (spec
   8.3).** Measured on the C6 under Arduino with TLS open: 155KB free and a
   131KB largest block, where buffer-then-parse fits. It is kept because it is
   O(1) in account size, not because it is required.

## License

Roboto Condensed is used under the SIL Open Font License; see
[core/tools/fonts/LICENSE-RobotoCondensed.txt](core/tools/fonts/LICENSE-RobotoCondensed.txt).
