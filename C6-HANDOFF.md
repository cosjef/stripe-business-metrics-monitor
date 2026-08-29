# ESP32-C6 AMOLED port — handoff

Branch: `port/esp32-c6-amoled`. Written 2026-08-29 after a long debugging
session. Everything works except **the display does not update after the first
frame**. That one bug is the whole remaining task.

---

## Read this first: the open bug

**Symptom.** The device boots, renders one frame, and never updates the glass
again. The serial log reports a successful redraw every 5 seconds. Every call
in the path returns `ESP_OK`. Nothing errors.

**What is proven, by measurement on hardware:**

| Test | Result |
|---|---|
| Waveshare's unmodified `09_LVGL_V9_Test` | **Works** — panel and hardware are fine |
| Their `lv_demo_widgets()` inside *our* project | **Works** — our sdkconfig, components and partition table are fine |
| Our `screen_draw_rotation()` drawn **once** from `app_main` | **Works** — our drawing code is fine |
| Two labels created once, then only `lv_label_set_text()` every 5s | **Works** — updates correctly |
| Our `screen_draw_rotation()` called repeatedly | **Blank** |

**Conclusion.** Rebuilding the LVGL object tree (`lv_obj_clean()` + recreating
objects) on a live display breaks this panel. Updating existing objects' text
works. The fault is not the driver, the adapter, the lock, the task, the stack,
the buffers or the panel — all were eliminated by the tests above.

**Where it was left.** `screens.c` was restructured around persistent objects
(create once, then update text/colour/visibility). Host tests pass, hardware
still blank. The remaining difference from the known-good build is two calls in
`screen_draw_rotation`: `lv_obj_set_style_text_color()` and a conditional
`lv_obj_set_pos()`.

**CORRECTION (2026-08-29, later session).** The bisect was run. **The two
calls are not the cause, and the "known-good" reference is not good.** Findings,
in the order they were established:

1. A probe on `LV_EVENT_INVALIDATE_AREA` was added to count what LVGL actually
   marks for repaint. All seven steps -- from text-only up to the full current
   code -- invalidated steadily and reported full-screen areas
   (`[0,0..479,479]`) every cycle. **The invalidation counter is worthless as a
   proxy**, exactly as this document warns about `ESP_OK`: step 6 logged +22
   invalidations per cycle while the glass was blank.
2. Step 6 (current code) on the glass: **blank**. Bug reproduces.
3. Step 0 -- the supposed known-good build, labels created once, positioned
   once, `lv_label_set_text` only -- on the glass: **also blank**. This is the
   result that matters. The fault is *outside* `screen_draw_rotation`; the
   premise that the delta was `lv_obj_set_style_text_color()` and the
   conditional `lv_obj_set_pos()` is wrong.
4. Waveshare's unmodified `09_LVGL_V9_Test`, flashed immediately after:
   **works**. The panel and board are healthy; the fault is in our project.

So the bug is somewhere in our bring-up or app structure that all seven steps
share -- not in the drawing calls. Suspects not yet eliminated: the
`display.cpp` / BSP init path, the rotation task, and whatever differs between
our `app_main` and theirs.

Per the owner's direction, ESP-IDF debugging stopped here and **the display
layer moves to Arduino/PlatformIO with Arduino_GFX** (the fallback this
document already agreed on, which Clawdmeter proves works on this board).

The bisect scaffold was fully reverted: `screens.c`, `main.c` and
`main/CMakeLists.txt` are byte-identical to before, `main/bisect.h` is deleted,
and the host suite still passes 1,526 checks across 27 suites.

---

## Hardware facts (hard-won, do not re-derive)

- **`LCD_CS` is GPIO15, not GPIO5.** Waveshare's own ESP-IDF example says 5 and
  is wrong for this board. Clawdmeter has 15. With the wrong CS nothing on the
  QSPI bus listens and every init step still reports success.
- **The AXP2101 PMIC gates the panel rails.** It must come up before the
  display. Its I2C address is `0x34` on GPIO 7 (SCL) / 8 (SDA).
- **Panel reset is a power rail, not a GPIO.** The CO5300's reset hangs off
  ALDO3; cycling that rail *is* the reset. Waveshare do it between
  `esp_lcd_new_panel_sh8601()` and `esp_lcd_panel_init()` — not in the PMIC
  step, and not omitted.
- **`esp_lvgl_adapter`, not `esp_lvgl_port`.** The port has no QSPI display
  registration and treats a QSPI panel as generic SPI. The adapter's migration
  guide lists QSPI support as the first thing it adds.
- **The adapter's lock takes `-1` for "wait forever".** `esp_lvgl_port` used
  `0` for that; passing `0` to the adapter means "do not wait" and every lock
  fails instantly, silently dropping all draws.
- **`swap_xy` is unsupported** by this panel; the driver logs an error
  unconditionally. It is noise, not a fault.
- **With a battery connected, `esptool --after hard_reset` does not reboot the
  board** — the PMIC holds power. Flashes land but never boot. Keep the battery
  out during development; `esptool.py --after hard_reset read_mac` forces a
  real reset.
- **Serial capture needs DTR/RTS false** before reading, or the chip is held in
  reset and the port returns silence.

## Working reference builds

- Waveshare example, cloned and confirmed working on this board:
  `/Users/cosjef/Development/ws_test/repo/02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test`
- Their BSP is vendored into `firmware/components/ws_bsp/` (unmodified).
  `display.cpp` delegates to it entirely.
- [Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter) — Arduino_GFX,
  supports this exact board, good for cross-checking hardware facts.
- [vibepulse](https://github.com/niclasvestlund-YT/vibepulse) — ESP-IDF, S3
  only. Uses `waveshare/esp32_s3_touch_amoled_2_16`; **no C6 equivalent exists
  in the registry** (checked, 404).

**If the display cannot be fixed in ESP-IDF, the agreed fallback is moving the
display layer to Arduino/PlatformIO with Arduino_GFX**, which Clawdmeter proves
works on this board.

---

## What works, and is the real value here

**The device fetches and computes live Stripe data correctly on the C6:**
`MRR $1,106.33 across 33 active subscriptions, in a single request.`

- **Streaming JSON parser** (`jsonstream.c`) — folds each subscription into a
  running total as bytes arrive. **912 bytes resident** versus 35,920 for
  buffer-then-parse. This is what makes the C6 viable: it has no PSRAM and a
  ~15KB largest free block, and the old approach forced one subscription per
  HTTP request (40+ round trips). Now one request.
- Pagination via `starting_after`, WiFi, captive portal, Stripe auth, NVS
  settings, flash cache, battery, geometry, history, sparkline.
- **27 suites, 1,526 host checks, 0 failures.**

**1,791 lines are hardware-independent and fully tested.** The S3 build on
`main` shares that core and works.

---

## Layout

The C6 is **2× the pixels but only 1.4× the physical size** (220 → 314 PPI), so
two scaling rules apply:

- **Type scales physically (×1.43).** Spec 2.2 states its legibility floor in
  millimetres at 50cm. A 96px hero is 11.1mm and must stay 11.1mm → **137px**.
- **Positions scale proportionally (×2).** Baselines are composition; the
  three-zone skeleton must keep its shape.

Doubling the type gives 15.5mm (~40% too large); copying it gives 7.8mm and
breaks the floor. Both are asserted in `test_layout_c6.c` and mutation-tested.

`layout.h` selects `layout_c6.h` or `layout_s3.h` by `CONFIG_IDF_TARGET_ESP32C6`.
Twelve font sizes were generated at the C6 ladder.

---

## Method notes

Two things cost the most time, both process rather than code:

**Establish ground truth first.** Flashing Waveshare's unmodified example took
ten minutes and proved the hardware was fine. Without it there was no way to
distinguish a real bug from damage introduced while debugging — and at one
point the screen was frozen by uncommitted changes rather than the original
fault.

**Bisect between two known-good points.** Once "their demo in our project" and
"our screen drawn once" were both known to work, the bug was cornered in four
steps. Roughly a dozen hypotheses tried before that — driver version, init
tables, byte order, buffer sizes, flush alignment, timer callbacks, stack
depth — all produced identical logs and a dark screen, because *every layer
reports success on this board even when nothing reaches the glass*.

`ESP_OK` is not evidence here. Only the glass is.

---

## State of the tree

Committed on `port/esp32-c6-amoled`:
- `91a66b4` Stream the JSON instead of buffering it
- `ea3c948` Bring up the C6 AMOLED port
- `89dfb79` Plot the sparkline, record capacity decisions

**Uncommitted** (the display debugging): `display.cpp` (replaces `display.c`),
`screens.c` restructuring, `board_config.h`, `idf_component.yml`,
`components/ws_bsp/`, `axp2101.c`, `main.c`, `stripe_api.c`.

`main` holds the working S3 build at `71fe82e` and is untouched.

## Build and flash

```sh
cd ~/Development/stripe_device/firmware
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem3101 flash          # port name changes between replugs
esptool.py --port /dev/cu.usbmodem3101 --after hard_reset read_mac   # force reboot
cd test && make && for t in ./test_*; do [ -x "$t" ] && $t; done
```

---

## Arduino port: stage 1 working (2026-08-29)

**LVGL now updates the glass repeatedly on the C6.** Confirmed by photo: the
probe screen shows a label, a green bar and a counter incrementing once a
second. This is what the ESP-IDF build could never do past frame one.

New tree: `arduino/`, a PlatformIO project. `firmware/` (ESP-IDF) is untouched.

**What made the difference.** Two things the IDF path never did, both taken
from Clawdmeter's `waveshare_amoled_216_c6` env, which runs on this exact
board:

1. **Manufacturer page-0x20 driving-voltage registers (0x19 = 0x10,
   0x1C = 0xA0).** `Arduino_CO5300::begin()` issues SLPOUT, pixel format,
   brightness, DISPON and MADCTL but not these. Clawdmeter's comment: without
   them "the panel stays black even with the rails up" -- our exact symptom.
   Whether this alone explains the IDF failure is untested; it is the leading
   candidate.
2. **The ALDO3 reset pulse** (HIGH/LOW/HIGH, 100ms holds) before any QSPI
   traffic. The IDF build did reset the rail, but not in this order.

**A trap worth recording: `-DLV_CONF_SKIP` silently disables all fonts.**
With `LV_CONF_SKIP`, every unset option takes its "no config" default, and for
the built-in fonts that default is **0** -- no glyphs compiled in, so labels
draw nothing while everything still reports success. Solid `fillScreen` calls
reached the glass and text did not, which reads exactly like a broken flush
path and cost a flash cycle. `LV_USE_THEME_DEFAULT` defaults to 0 the same
way. Both are now set explicitly in `platformio.ini` with this rationale.

**Bring-up order (not interchangeable):** PMIC rails + ALDO3 pulse ->
`display_init()` (QSPI, CO5300, driving voltages) -> `display_lvgl_init()`.

**Verified good and not to be re-derived:** LCD_CS is GPIO15; there is no
reset GPIO (ALDO3 is the reset) and no backlight GPIO (brightness is a panel
command); the CO5300 needs even-aligned flush windows, rounded *outward*.

### Flashing quirks hit this session

- The board stopped accepting flashes mid-session -- esptool could not sync
  while the port still enumerated. A replug fixed it.
- After a replug it comes up in **download mode** (`boot:0x77`,
  "waiting for download") and stays there through `--after hard_reset`. It
  needs a clean replug with BOOT released to boot normally (`boot:0x7f`).

```sh
cd arduino
pio run
pio run -t upload --upload-port /dev/cu.usbmodem3101
```

### Stage 2 done: the deck rotates

Confirmed on the glass: all nine screens cycling every 5s at C6 geometry
(`layout: panel 480px, pad 23, hero 35-137px`), on fixture values.

**The rendering core is shared, not copied.** `platformio.ini` compiles
`screens.c`, `hero_size.c`, `baseline.c` and `fonts/` directly out of
`firmware/main/` via `build_src_filter`. Both builds therefore run the same
code, and the host suite in `firmware/test` keeps covering what ships. The
fonts alone are 8.6MB of source; there should be exactly one copy.

**Target selection needed fixing in two places.** `layout.h` and
`hero_size.c` each chose their geometry on `CONFIG_IDF_TARGET_ESP32C6`, which
comes from `sdkconfig.h` and does not exist under Arduino -- so the C6 would
silently take the **S3** ladder and render a deck typed for the wrong panel,
happily and without error. Both now also accept `BOARD_C6_AMOLED_216`.

Because those are two separate conditions that must agree, `hero_size.c`
carries `_Static_assert`s tying the selected ladder to `SIZE_HERO_MAX` /
`SIZE_HERO_MIN` from `layout.h`. Mutation-tested: forcing the C6 ladder while
layout picks S3 fails the build with `(96) == 137`. A comment would not have
caught the drift; this does.

### Next

Stage 3: WiFi, NVS settings, captive portal. Stage 4: the streaming Stripe
fetch (`jsonstream.c`), which is the piece that makes the C6 viable at 912
bytes resident.


---

## UX redesign: parked ideas

The deck's layouts are being reworked for the C6 (the originals are the S3's
240x240 composition scaled up). Two ideas came out of that work that are worth
building but are not being built yet.

**Measured on the live account 2026-08-29**, which is what made these look
worth having: 33 active, +10 new and -7 churned over 30 days (net +3, a 17.5%
monthly churn rate), median tenure 80 days, and **two subscribers with
`cancel_at_period_end` set, worth $42/mo**.

### NEXT UP: at-risk revenue -> SCREEN_CANCELLATIONS

Agreed 2026-08-29 as the screen to build after PAID SUBS.

The most actionable fact in the account is that $42/mo has given notice but
has not left yet. Nothing on the device shows it.

`SCREEN_CANCELLATIONS` already exists in `rotation.h`, already has a
visibility rule (shown whenever there is data -- zero cancellations in a
month is itself worth knowing), and already has a label. It is unfilled only
because nothing computes churn. That is its screen; do not invent a new one.

It should carry both the 30-day churn count and the at-risk figure, because
"7 left" and "2 more are leaving" are different facts and the second is the
one that can still be acted on.

### Parked: tenure

Median active tenure (80 days here) is a real measure of stickiness and would
make a good screen of its own. Deliberately deferred: it drifts rather than
moves, so it is worth less per rotation slot than flow or churn.

### Rejected: tier composition

A segmented bar of price tiers ($29 x14, $49 x12, $13 x6) was mocked and
rejected. It renders well and the data is real, but tiers change only when
the owner reprices -- perhaps twice a year. A glanceable device should
spend its space on what moves. Kept here so the idea is not re-proposed.

The mock lives in `firmware/test/mock_mrr.c` (render_subs) if it is ever
wanted.
