# Stripe Revenue Display

Firmware for a single-purpose desk instrument that shows live Stripe revenue
metrics. It answers one question at a time, in numbers readable across a room,
and talks to nothing except the Stripe API.

## The deck

Eight screens, five seconds each. A screen with nothing to say hides itself,
so the rotation is only ever as long as the account warrants.

| | | |
|:--:|:--:|:--:|
| ![MRR](docs/img/mrr.png) | ![New paid](docs/img/new_paid.png) | ![Paid subs](docs/img/paid_subs.png) |
| **MRR** — the anchor, with its 30-day trend | **NEW PAID** — signups, and whether they are speeding up | **PAID SUBS** — the flow behind the count |
| ![Cancelled](docs/img/cancelled.png) | ![ARR](docs/img/arr.png) | ![ARPU](docs/img/arpu.png) |
| **CANCELLED** — revenue that gave notice but has not left | **ARR** — the annual figure, in annual units | **ARPU** — are the customers won worth more than those lost |
| ![Net 30d](docs/img/net_30d.png) | ![Failed](docs/img/failed.png) | ![Stale](docs/img/stale.png) |
| **NET 30D** — what the month did to revenue | **FAILED** — the only screen that earns red | **stale** — shown instead of a figure it can no longer vouch for |

The screenshots are rendered from the firmware itself, at the panel's real
480x480 with the real fonts, by `core/test/render_docs`. They cannot drift
from what the device draws.

A few of the decisions they show:

- **A count is not a story.** "33 subscribers" reads the same whether the
  month added three or added ten and lost seven. PAID SUBS shows the flow;
  NET 30D shows what it did to the money.
- **Green means realized gain, and nothing else.** Amber is a degraded state,
  red is a threshold breach that can still be acted on. FAILED is the only
  screen that earns red.
- **The device does not guess.** A trend needs seven daily samples before it
  is drawn, an ARPU comparison needs six customers on each side, and a figure
  it cannot vouch for is shown as stale rather than presented as current.

## Layout

```
core/           portable: rendering, MRR maths, parsers, fonts, tests
firmware-c6/    the device: display, radios, provisioning, I2C peripherals
docs/           the spec, the port plan, and the hardware notes
```

Nothing in `core/` may include `esp_*.h`, `<Arduino.h>`, `driver/*` or
`freertos/*`. That single constraint is what lets 1,471 checks run on a laptop
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

1,471 checks across 24 suites. Most cover pure logic — text measurement, hero
auto-sizing, MRR arithmetic, the streaming JSON scanners, rotation rules,
freshness, battery thresholds, WiFi retry policy. One boots real LVGL against
an offscreen framebuffer and asserts on actual pixels.

`make quick` skips the LVGL build and runs only the logic suites. LVGL comes
from the PlatformIO dependency: `cd firmware-c6 && pio pkg install`.

Panel bring-up, the radios and the I2C peripherals are not covered — they need
real hardware, and every claim about them in the docs was checked against a
register read rather than a datasheet.

## Regenerating the screenshots

The README's images come from the firmware, not from a drawing tool. After any
layout change:

```sh
cd core/test && make render_docs && ./render_docs
../../docs/img/build.sh          # needs ImageMagick
```

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

## Known limitations

- **The timezone is hardcoded** to `EST5EDT` in `firmware-c6/src/main.cpp`.
  It decides when the daily history rolls over, so anywhere outside US Eastern
  the day boundary lands at the wrong hour. Change `DEVICE_TZ` before
  flashing.
- **NVS is unencrypted.** A deliberate trade: the stored key is read-only and
  restricted, so it leaks a subscriber count rather than the ability to move
  money.

[docs/firmware-build-plan.md](docs/firmware-build-plan.md) has the full list
and what is deliberately not built.

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

MIT — see [LICENSE](LICENSE).

Roboto Condensed is vendored under the SIL Open Font License; see
[core/tools/fonts/LICENSE-RobotoCondensed.txt](core/tools/fonts/LICENSE-RobotoCondensed.txt).
The generated font files in `core/fonts/` are derived from it and carry the
same terms.

Not affiliated with Stripe. "Stripe" is their trademark; this project only
reads their public API.
