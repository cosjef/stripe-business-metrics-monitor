# Port Plan: ESP32-C6 Touch AMOLED 2.16"

Porting the Stripe revenue display from the Waveshare ESP32-S3-LCD-1.54
(240x240 IPS, no touch) to the ESP32-C6-Touch-AMOLED-2.16 (480x480 AMOLED,
capacitive touch).

Branch: `port/esp32-c6-amoled`. Written 2026-08-18, hardware arrives in ~10 days.

**Same core functionality.** The device still shows live Stripe metrics on a
rotating deck. What changes is that the new hardware removes two constraints
that shaped the current design, and adds one that is harder than it looks.

---

## Connectivity is unchanged: WiFi

The C6 adds WiFi 6, Bluetooth 5 and 802.15.4 (Thread/Zigbee), but the
transport for this device does not change. The Stripe API is HTTPS over
TCP/IP, and Bluetooth connects to a nearby phone, not to the internet. Using
BLE would mean a phone relaying every request — adding a dependency on your
phone being present and running an app, to a device whose value is sitting on
a desk working unattended.

The one place BLE is genuinely attractive is **provisioning**: replacing the
captive portal with BLE setup is a nicer first-run experience and is well
supported (`esp_wifi_provisioning`). Worth considering later; it changes setup
only, not how data is fetched.

Thread/Zigbee are for mesh sensor networks and have no path to `api.stripe.com`.

---

## The headline: what actually changes

| | S3 (current) | C6 (new) | Consequence |
|---|---|---|---|
| PSRAM | 8MB | **none** | 912KB of buffers must shrink. Pagination handles it; see C2. |
| SRAM | 512KB + PSRAM | 512KB total | Everything competes for one pool |
| Display | 240x240 IPS | 480x480 AMOLED | 4x pixels; true black; bigger type |
| Black | `#000000` forced | `#121211` viable | The spec's original choice becomes correct |
| Input | 1 button | capacitive touch + 2 buttons | Swipe navigation, finally |
| Power | ADC divider on GPIO1 | AXP2101 PMIC over I2C | Real fuel gauge, not a voltage guess |

Everything else is either unchanged or an improvement that falls out for free.

---

## What ports untouched

This is the payoff for the host-testable split maintained throughout the
build. **17 of 27 source files (1,693 lines) contain no ESP-IDF dependency:**

```
baseline.c  battery.c  cache.c     events.c    format.c    freshness.c
hero_size.c mrr.c      orientation.c  provision.c  rotation.c  screens.c
state.c     stripe_key.c  stripe_parse.c  tapdetect.c  tapstatus.c
```

**981 of 1,222 test checks port unchanged** — every suite except the three
that assert pixels against a 240x240 framebuffer:

| Ports as-is | Needs rework |
|---|---|
| test_mrr (58), test_format (64), test_events (58), test_freshness (38), test_rotation (34), test_cache (29), test_state (42), test_battery (54), test_stripe_parse (48), test_stripe_key (37), test_provision (33), test_hero_size (51), test_orientation (29), test_tapdetect (69), test_tapstatus (337) | test_screens (109), test_layout (96), test_font_coverage (36) |
| **981 checks** | **241 checks** |

The MRR engine, the rotation rules, the freshness/backoff logic, the state
precedence, the cache format and the money formatter all move over as source
files with no edits. That is the bulk of the thinking in this project, and it
is safe.

One caveat on "no edits": `screens.c` depends on LVGL and `stripe_parse.c` on
cJSON. Neither is ESP-IDF and both run on the C6 — LVGL 9.5.0 is the version
Waveshare's own example uses, and cJSON is portable C. Under the pagination
approach in Stage C2, `stripe_parse.c` genuinely does not change: a page of 10
subscriptions has the same JSON shape as a page of 100.

---

## Stage C1 — Board bring-up

Waveshare publishes an ESP-IDF v5.5.3 example set for this exact board, and
critically **an LVGL v9 example using LVGL 9.5.0** — the same version we run.
This removes most of the guesswork that made Stage 1 take five attempts.

**Verified in `02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test`** (`user_config.h`,
`idf_component.yml`, `main.cpp`):

```
Display driver : espressif/esp_lcd_sh8601 ^1.0.0
Touch driver   : waveshare/esp_lcd_touch_cst9217 ^1.0.4
PMIC           : AXP2101 @ I2C 0x34
LVGL           : lvgl/lvgl ^9.5.0   (our dependencies.lock resolves to 9.5.0)
Adapter        : espressif/esp_lvgl_adapter ^0.3.0
                 NOT esp_lvgl_port (we use ^2.6.0) -- different API
Resolution     : 480x480, RGB565 (BITS_PER_PIXEL 16)

QSPI      : CS=5  PCLK=0  D0=1  D1=2  D2=3  D3=4
I2C       : SCL=7  SDA=8
Touch     : RST=11  INT=15
Backlight : no GPIO (GPIO_NUM_NC) -- brightness via Set_Backlight(0-100)
Reset     : no GPIO (GPIO_NUM_NC) -- panel reset over QSPI
```

**From the Waveshare product page, NOT from the example code:** the display
controller is described as CO5300 and the touch controller as CST9220. Neither
part number appears anywhere in the example sources — the only identifiers
there are the driver package names above (`sh8601`, `cst9217`). The drivers are
what we actually build against, so this matters little in practice, but do not
treat the marketing part numbers as verified. Confirm against the silkscreen
or an I2C scan when the board arrives.

Tasks:
- [ ] New `board_config.h` from the pin table above
- [ ] Swap `esp_lcd_panel_st7789` for `esp_lcd_sh8601`; QSPI bus config
- [ ] Evaluate `esp_lvgl_adapter` vs our current `esp_lvgl_port` — the
      Waveshare example uses the former; porting cost is small either way,
      but the APIs differ and this decision gates `display.c`
- [ ] Backlight via panel command, not GPIO PWM (`Set_Backlight(0-100)`)
- [ ] Confirm `orientation.c` still applies — it is pure logic and ports,
      but the correct angle for the new enclosure is unknown until it arrives

**Deliberately deferred to hardware arrival:** the display bring-up colour
diagnostic. On the S3 this took five attempts and was only settled by
building `colortest.c`. Build that first this time, not fifth.

---

## Stage C2 — Fitting the fetch into 512KB

**Corrected 2026-08-18.** An earlier version of this plan called a streaming
JSON parser mandatory and "the only part that is genuinely difficult". That
was wrong. Pagination solves the same problem with far less risk, and it is
the recommended approach.

### The actual numbers

```
raw JSON buffered today   512 KB   (subscriptions, limit=100)
data we retain from it     25 KB   (sizeof(stripe_subs_t) = 25,616 bytes)
                          ------
ratio                      ~20x more raw JSON than kept data
```

The retained 25KB is `items[512]` at 40 bytes each (20,480) plus `subs[128]`
at 40 bytes each (5,120), plus scalars. Each `mrr_item_t` holds the eight
fields we actually use — `unit_amount`, `quantity`, `interval`,
`interval_count`, `recurring`, `tiered`, `currency`, and the discount — in 40
bytes.

The 512KB exists only because we ask Stripe for 100 subscriptions at once and
each expanded object is 3-4KB of JSON. It is sized for the wire format, not
for what we keep.

### Option A — pagination (recommended)

Ask for fewer subscriptions per request and follow `has_more`.

```
limit=100  ->  ~350 KB buffer    2 pages
limit=25   ->   ~88 KB buffer    6 pages
limit=10   ->   ~35 KB buffer   13 pages
```

At `limit=10` the buffer is ~35KB, which fits the C6 comfortably.

What makes this cheap: **`stripe_parse.c` does not change at all.** It already
parses `has_more` (line 128) and that is already covered by
`test_stripe_parse`'s `test_has_more`. Its 48 tests keep passing untouched,
because the shape of a page response is identical regardless of how many
items it holds. The new code is a fetch loop plus a `starting_after` cursor —
on the order of 40 lines, in `stripe_api.c` only.

Cost: roughly 13 HTTP round trips per full refresh instead of 2. TLS is
already established, so each is ~100-200ms — about 2 seconds added to a
refresh that runs every 10 minutes.

### Option B — streaming parse

Parse incrementally from the HTTP read callback and never buffer a full body.
Gets the buffer to ~4KB. Requires replacing cJSON with a tokenizer and
rewriting `stripe_parse.c`, whose 48 tests define the behaviour that rewrite
must preserve. Chunk-boundary handling is the classic failure mode and is
fiddly to get right.

**Not needed.** Keep it in reserve only if pagination somehow proves
insufficient, which the arithmetic above says it will not.

### The S3 has the same ceiling (deferred)

Today `has_more` is surfaced but never followed: `stripe_api.c` contains no
`starting_after`. An account with more than 100 subscriptions gets its MRR
computed from the first 100 only.

The failure is handled honestly rather than silently — `main.c` renders
"partial" as the MRR subtitle when `truncated` is set — so this is a stated
limitation, not a wrong number. But it is still a ceiling, and pagination
removes it. **Deferred by decision (2026-08-18): not building this yet.** The account is
at 29 subscriptions against a ceiling of 100, so the limitation is not
reachable and the honest "partial" subtitle covers it if it ever is. Building
pagination now would be speculative work against a bound we are nowhere near.

It becomes necessary — and stops being optional — **when the C6 port starts**,
because that is when the 512KB buffer has to shrink. At that point it is the
mechanism, not an improvement.

When it is built, the work is:

- [ ] Add `starting_after` cursor and a fetch loop in `stripe_api.c`
- [ ] Test: multi-page fixture where page 2 completes the total, asserting
      the summed MRR matches a single-page fixture of the same subscriptions
- [ ] Test: `has_more` true but next page empty (Stripe edge case)
- [ ] Reduce `SUBS_BUF_LEN`; same for events and invoices
- [ ] Retire the "partial" subtitle once truncation is unreachable

Watch for: if the subscription count approaches 100 before the port, this
stops being deferrable on the S3 as well.

---

### Flash budget: not a constraint

Worth stating plainly, because an earlier draft got this wrong. The C6 has
**16MB of external flash**. The 6MB figure is our *own* `partitions.csv`
choice (Waveshare's example splits 6M app + 3M spiffs) — neither is imposed
by the chip. Fixed overhead is about 64KB (bootloader, partition table, nvs,
phy_init), so **~15.9MB is allocatable** as we see fit.

Current usage: a 1.84MB binary in a 6M partition, 30% full. Even doubling
every font lands around 3.6MB. A 6M+6M dual-app OTA layout still fits.
Nothing here is tight.

---

## Stage C3 — Layout at 480x480

4x the pixels, and the type can roughly double.

```
                  240x240        480x480
text column       208px          416px
hero cap          96px           see below
```

**The hero cap is not simply 2x.** Doubling the column to 416px does not mean
doubling the cap to 192px: at 192px, `$11.2k` measures 535px and `$970.33`
measures 638px, both of which overflow. The cap should be re-derived from the
longest string we actually intend to render, not scaled linearly. What the
wider column buys is that *today's* 96px values all fit comfortably, with room
to raise the cap somewhat for short values — the exact ceiling is a
`hero_size.c` question to settle with measurements, not arithmetic.

**The `$11k` decision reverses.** We dropped the decimal from ARR three
commits ago because `$11.2k` needed 268px against a 208px column. At 416px it
fits at full size, and so does `$970.33` exact (measured 319px at 96px). The
compromise was forced by the small panel, not by taste.

- [ ] Regenerate fonts at 2x sizes
- [ ] **Flash is not a constraint. Ship all 12 sizes.** An earlier draft of
      this plan claimed a naive 2x regeneration would need ~11.9MB and not
      fit. That was wrong: it scaled the 2.9MB of `.c` source on disk, which
      is ASCII hex arrays, roughly 5x larger than the compiled data. Measured
      from the linked ELF, the 12 sizes contribute **590KB**, so 2x is
      about **2.4MB** — comfortable inside the existing 6M app partition,
      with ~2.5MB spare.
- [ ] Rework `layout.h` constants; `hero_size.c` logic is unchanged (it reads
      `TEXT_COLUMN_PX` and the size table)
- [ ] Rework `test_layout` / `test_screens` for the new geometry (241 checks)
- [ ] Revisit `format.c`'s 10k decimal threshold once the column is wider

**Opportunity, not required:** at 480x480 a screen can hold a hero *and* a
sparkline, or two metrics side by side. Worth resisting initially. The spec's
"one number, glanceable" discipline is why this device works, and 4x the
pixels is 4x the room to clutter it. Recommend porting the existing three-zone
layout first, verifying it reads well at the new size, and only then
considering what genuinely earns space.

---

## Stage C4 — Colour, freed

The most interesting change. Section 3.1 of the spec chose `#121211` over
pure black, and we overrode it after `colortest.c` proved this IPS panel
collapses everything from `0x04` to `0x30` into the same mid-gray.

**AMOLED inverts that entirely.** Black pixels are simply off — true black,
effectively infinite contrast, and near-black values render as intended
instead of collapsing.

- [ ] Re-run the colour ramp diagnostic on the AMOLED to confirm
- [ ] Restore `#121211` as `COLOR_BG` if confirmed (spec-compliant at last)
- [ ] **Re-derive the accent palette.** `COLOR_RED #E74D63` was chosen for
      5.64:1 against `#000000` *and* exact RGB565 round-trip. Against a
      different background the contrast maths changes, and 16.7M colour means
      the RGB565 round-trip constraint may relax to RGB888
- [ ] Keep the discipline regardless: green = realized gains only, red =
      threshold breach only. `test_screens`' colour-discipline checks port
      directly and should keep guarding this
- [ ] **Power note:** on AMOLED, dark pixels cost less energy. The dark theme
      is now a battery feature, not just an aesthetic one — which also makes
      Stage 8 night dimming cheaper than it was

---

## Stage C5 — Touch

The thing you asked for back in Stage 2 and could not have.

- [ ] `esp_lcd_touch_cst9217` + LVGL `lv_indev` pointer device
- [ ] **Swipe left/right to change screens** — the original request. Note
      `esp_lcd_touch` provides raw coordinates only; gestures come from LVGL's
      `LV_EVENT_GESTURE` on an indev, not from the driver
- [ ] Tap to pause/resume rotation (a glance-and-hold gesture)
- [ ] Keep both physical buttons working — touch is an addition, not a
      replacement. A device you can drive without looking at it is better.
- [x] **`tapdetect.c` / `tapstatus.c` stay.** (Decided 2026-08-18.) These are
      the IMU tap-detection modules built and abandoned on the S3 when the
      QMI8658 hardware tap engine never fired; they carry 406 of our 1,222
      checks. I had recommended deleting them as dead weight — overruled, and
      the C6 gives a reason to keep them: **it carries the same QMI8658**
      (Waveshare example `02_I2C_QMI8658`), so the host-side detection logic
      that worked on the S3 is directly reusable. Both files are pure logic
      with no ESP-IDF dependency, so they port for free and cost nothing but
      flash.
- [ ] Consider reviving IMU tap as a *complement* to touch rather than a
      replacement: on the S3 the blocker was that corrupt I2C reads and real
      taps both landed in single 20ms samples. Touch now provides an
      independent signal that could disambiguate them — a tap confirmed by
      neither touch nor the button is more likely noise.

---

## Stage C6 — Power via AXP2101

Genuine upgrade over the current setup.

The S3 reads a voltage divider on GPIO1 and *infers* USB presence from
whether the rail sits above 4.15V — an inference documented in `battery_hw.c`
as "not measurement". The AXP2101 reports charge state directly over I2C.

- [ ] AXP2101 driver at I2C 0x34 (Waveshare example `01_AXP2101_Test`)
- [ ] Replace `battery_hw.c` entirely; **`battery.c` is unchanged** — the
      thresholds, hysteresis and plausibility logic are pure and its 54 tests
      still apply
- [ ] Delete the `usb_present()` voltage heuristic; use the real charge flag
- [ ] Possible: real charge percentage from the PMIC rather than our linear
      voltage approximation
- [ ] PCF85063 RTC on board — could hold time across power loss and reduce
      NTP dependence. Nice-to-have, not required.

---

## Sequencing

Ordered by risk, not by visibility. C2 shrinks the fetch to fit 512KB of
SRAM, and nothing else can run on the C6 until it does — so it leads, even
though it is invisible on the glass.

| Order | Stage | Why here | Needs hardware? |
|---|---|---|---|
| 1 | **C2 pagination** | Removes the PSRAM dependency. Deferred on the S3 (29 of 100 subs), but required before C1 can run on the C6. | **No** — testable on host |
| 2 | C3 layout maths | Pure logic + tests; no device needed | No |
| 3 | C1 board bring-up | First thing on arrival; build colortest *first* | Yes |
| 4 | C4 colour | Needs the ramp diagnostic on real glass | Yes |
| 5 | C5 touch | Needs hardware | Yes |
| 6 | C6 AXP2101 | Needs hardware; `battery.c` already done | Yes |

**Work available before the board arrives (~10 days):** C2 and C3 are both
host-testable with no hardware. C2 is deferred by decision until the port
proper begins, which leaves **C3 (layout maths at 480x480)** as the natural
thing to start on — pure logic, existing tests, no device required.

---

## Open questions

1. ~~**`esp_lvgl_adapter` or `esp_lvgl_port`?**~~ — **Resolved: keep
   `esp_lvgl_port`.** Its manifest declares `idf: '>=5.2'` and
   `lvgl: '>=8,<10'` with **no target restriction**, so the C6 is supported.
   Waveshare's example uses `esp_lvgl_adapter`, but nothing forces us to
   follow it — the display driver (`esp_lcd_sh8601`) and the LVGL port are
   independent choices. Keeping `esp_lvgl_port` means `display.c` changes
   only in which panel driver it instantiates, and every LVGL call above it
   stays identical. If bring-up hits an adapter-specific quirk in Waveshare's
   init sequence, revisit then.
2. ~~**Delete `tapdetect`/`tapstatus`?**~~ — **Resolved: keep both.** The C6
   has the same QMI8658, and both files are pure logic that ports for free.
3. **Keep the S3 build?** Dual-target adds CMake and CI complexity. Suggest
   the C6 becomes `main` once it works, with the S3 kept on a tag.
4. ~~**Font sizes to ship**~~ — **Resolved: ship all 12.** The constraint was
   an arithmetic error on my part (source size vs compiled size); measured
   from the ELF, 2x costs ~2.4MB against a 6M partition. See Stage C3.
5. **Physical enclosure** — orientation angle unknown until the board arrives;
   `orientation.c` handles it, but the constant needs setting by eye.

---

## Decisions log

Recorded so the reasoning survives, and so reversals are visible rather than
quietly rewritten.

| Date | Decision | Reasoning |
|---|---|---|
| 2026-08-18 | **Connectivity stays WiFi** | Bluetooth reaches a phone, not the internet. BLE relaying would make an unattended desk device depend on a phone being present. BLE *provisioning* noted as a separate future idea. |
| 2026-08-18 | **Pagination, not streaming** | 512KB buffered vs 25KB retained; `limit=10` gives a ~35KB buffer. Leaves `stripe_parse.c` and its 48 tests untouched. Streaming would rewrite the parser for no additional benefit. **Reverses my earlier claim that streaming was mandatory.** |
| 2026-08-18 | **Pagination deferred on the S3** | 29 subscriptions against a ceiling of 100; the "partial" subtitle handles the limit honestly. Speculative work against an unreachable bound. Becomes required when the port begins. |
| 2026-08-18 | **Keep `tapdetect` / `tapstatus`** | The C6 carries the same QMI8658; both files are pure logic and port free. Touch may also give the independent signal needed to disambiguate real taps from the I2C noise that defeated the S3 attempt. |
| 2026-08-18 | **Keep `esp_lvgl_port`** | Manifest declares `idf >=5.2`, `lvgl >=8,<10`, no target restriction — the C6 is supported. Waveshare use `esp_lvgl_adapter`, but panel driver and LVGL port are independent choices. |
| 2026-08-18 | **Ship all 12 font sizes** | Reverses an earlier recommendation to cut to 5-6. That rested on an arithmetic error: 2.9MB is `.c` source (ASCII hex), while the linked data is 590KB. 2x costs ~2.4MB against a 6M partition. |

### Corrections to earlier drafts of this plan

Kept visible rather than silently edited, since several were stated with
misplaced confidence:

- **"Streaming JSON is mandatory"** — wrong. Pagination is sufficient and far
  cheaper. This was the plan's central risk claim and it did not survive
  scrutiny.
- **"12 fonts at 2x need ~11.9MB"** — wrong, off by ~5x. Scaled the on-disk
  source rather than compiled data. A follow-up measurement was also wrong
  (1,126KB) because the build tree held stale `stripe_mono_*` objects from the
  abandoned monospace experiment.
- **"6MB app partition"** — presented as a chip constraint; it is our own
  `partitions.csv` choice against 16MB of flash.
- **"CO5300 / CST9220 confirmed from the example code"** — those part numbers
  appear nowhere in it. They come from the product page. The example only
  names the drivers `esp_lcd_sh8601` and `esp_lcd_touch_cst9217`.
- **"$970.33 is 340px"** — measured 319px.
- **"Hero cap doubles to 192px"** — at 192px the very strings the section
  argues for overflow the 416px column.
- **"26 source files"** — 27.

---

## What is NOT changing

Worth stating, because it is most of the value:

- The MRR computation and its 58 tests
- Rotation rules, conditional screens, and their 34 tests
- Freshness, staleness, exponential backoff — 38 tests
- Display-state precedence (setup > auth > battery > stale > rotation) — 42 tests
- The flash cache format and its 29 tests
- Money formatting and its 64 tests
- Green/red accent discipline
- The three-zone layout *grammar* (label / hero / subtitle), even as the
  numbers change
