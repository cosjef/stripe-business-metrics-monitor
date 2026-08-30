# Stripe Revenue Display

Firmware for a single-purpose desk instrument that shows live Stripe revenue
metrics. It answers one question at a time, in numbers readable across a room,
and talks to nothing except the Stripe API.

Not affiliated with Stripe.

## Layout

```
core/           portable: rendering, MRR maths, parsers, fonts, tests
firmware-c6/    the device: display, radios, provisioning, I2C peripherals
docs/           the spec, the port plan, and the hardware notes
```

Nothing in `core/` may include `esp_*.h`, `<Arduino.h>`, `driver/*` or
`freertos/*`. That single constraint is what lets 1,403 checks run on a laptop
with no hardware and no SDK. See [core/README.md](core/README.md).

## Status

Working. The device provisions itself over a captive portal, stores
credentials in NVS, fetches from Stripe over TLS with a pinned CA, and rotates
eight screens: MRR, new paid, paid subs, cancelled, annual run rate, ARPU, net
30-day, and failed payments. Screens with nothing to say hide themselves.
Buttons and touch move the deck, the battery shows in the label row, and a
failed fetch surfaces a stale screen rather than presenting old numbers as
current.

It was first attempted in ESP-IDF and never drove the panel past its opening
frame. [docs/C6-HANDOFF.md](docs/C6-HANDOFF.md) records that failure, the
bisect that could not find it, and the hardware facts that cost the most to
establish -- several of which contradict Waveshare's own documentation.

See [docs/stripe-revenue-display-spec.md](docs/stripe-revenue-display-spec.md)
for the design this is built from.

## Building

```sh
cd firmware-c6
pio run
pio run -t upload --upload-port /dev/cu.usbmodem<N>
```

No credentials at build time: the device provisions over its own access point.
The serial port name changes between replugs -- check `ls /dev/cu.usbmodem*`
first.

## Tests

Everything portable is tested on the host, with no hardware and no SDK:

```sh
cd core/test
make
for t in ./test_*; do [ -x "$t" ] && $t; done
```

1,403 checks across 25 suites. Most cover pure logic — text measurement, hero
auto-sizing, MRR arithmetic, the streaming JSON scanners, rotation rules,
freshness, battery thresholds, WiFi retry policy. One boots real LVGL against
an offscreen framebuffer and asserts on actual pixels.

`make quick` skips the LVGL build and runs only the logic suites. LVGL comes
from the PlatformIO dependency: `cd firmware-c6 && pio pkg install`.

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

1. **Background is `#000000`, not `#121211` (spec 4.1/3.1).** Inherited from
   an IPS panel where `0x04`-`0x30` collapsed to the same mid-gray. Unverified
   on this AMOLED, where the reasoning does not carry over -- a black pixel
   here is simply off, so the spec's `#121211` may well be viable. Measure
   before changing it.

2. **Typeface is Roboto Condensed, not monospace (spec 5.4).** Monospace
   spends a full character cell on `.`, shrinking digits enough to hurt
   legibility at 50cm. Roboto Condensed has tabular figures, so it keeps the
   anti-jitter property that motivated the monospace rule anyway.

3. **Streaming JSON is kept, but not for the reason the spec gives (spec
   8.3).** Measured with TLS open: 155KB free and a 131KB largest block, where
   buffer-then-parse fits. It is kept because it is O(1) in account size, not
   because it is required.

## License

Roboto Condensed is used under the SIL Open Font License; see
[core/tools/fonts/LICENSE-RobotoCondensed.txt](core/tools/fonts/LICENSE-RobotoCondensed.txt).
