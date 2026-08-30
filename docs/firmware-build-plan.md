# Firmware Build Plan (historical)

> **This describes the original ESP32-S3 build and is kept as a record, not as
> a roadmap.** Its stages and checkboxes track a device this repository no
> longer contains; the C6 port that replaced it is documented in
> [C6-HANDOFF.md](C6-HANDOFF.md), and the current state of the firmware is in
> the top-level README. Nothing here is a statement about what works today.

Tracked implementation progress against
[stripe-revenue-display-spec.md](stripe-revenue-display-spec.md).

---

## Status as of 2026-08-15

**The device is finished and running on live Stripe data.** Stages 1-7 are complete. Stage 8 is skipped by decision, and everything else is parked.

**Ten screens are defined; the deck shows however many currently have something to say** — conditional screens drop out, so the live count on this account is 7-8, not a fixed number. In rotation order, grouped by kind:

| Group | Screens |
|---|---|
| Revenue | MRR, ANNUAL RUN RATE, ARPU |
| Alert | FAILED *(only when a payment has actually failed)* |
| Movement | NET 30D, NEW PAID, CANCELLED |
| Composition | PAID SUBS, TRIALS *(hidden — no trials on this account)*, CONVERSION *(hidden)* |

Each screen holds for 5 seconds, with manual advance on the leftmost button. Data refreshes fully every 10 minutes and incrementally every 60 seconds, survives reboots via flash cache, and degrades to a stale or auth-error screen rather than showing a confident wrong number.

**Tests: 16 suites, 1,100 checks, 0 failures.** Host-side, rendering through real LVGL into an offscreen framebuffer, so screen output is asserted pixel by pixel rather than by eye.

Three deviations from the spec are deliberate and documented below: the background is `#000000` (the panel physically cannot render `#121211`), the typeface is Roboto Condensed rather than monospace, and there is no touch input (the board has no touch controller — navigation is the physical button).

**Stages 1-7 are complete with nothing left unchecked.** Everything remaining is in Parked below, and none of it blocks the device as it stands.

---

## Toolchain decisions

| Area | Choice | Rationale |
|---|---|---|
| Hardware | Waveshare ESP32-S3-LCD-1.54 (non-touch), ST7789, 240x240, SPI, 8MB PSRAM, 16MB flash | Confirmed via product research 2026-08-15. PSRAM headroom means the spec's "must stream-parse, 300KB won't fit in heap" concern (§8.3) is likely avoidable — full-buffer JSON parsing may be viable. Revisit at Stage 4/5. |
| Framework | ESP-IDF (not Arduino) | User choice. More setup than Arduino but no HAL abstraction tax. |
| Graphics | `esp_lcd` (`esp_lcd_panel_st7789`) + `esp_lvgl_port` + LVGL 9 | This is what Waveshare's own ESP-IDF example for this exact board uses. Confirmed 2026-08-15 via Waveshare's repo (`examples/ESP32-S3-LCD-1.54-demo/ESP-IDF-5.5.1/05_lvgl_example`). Rejected LovyanGFX for IDF — technically possible but thinly documented outside Arduino, not what Waveshare ships. |
| Starting point | Waveshare `ESP32-S3-Touch-LCD-1.54` GitHub repo, `05_lvgl_example` | Drop the `esp_lcd_touch_cst816s` dependency. **Confirmed by I2C scan** that no touch controller exists on this board — see "Input: no touch controller" below. |
| Input | **Single hard tap**, detected HOST-SIDE from raw accelerometer magnitude (QMI8658 at I2C `0x6B`) | No touch hardware. Departs from spec §1 principle 3 ("no interaction"). The chip's own tap engine never fires on this part; ships with a documented false-trigger trade-off. See below. |
| Fonts | **Roboto Condensed Bold (SIL OFL)**, generated to LVGL bitmap fonts at 12 sizes (18/20/22 UI + 24/32/40/52/60/64/76/88/96 hero) via `tools/gen_fonts.sh` | See "Typeface deviation from spec §5.4" below. LVGL's stock Montserrat was rejected (caps at 48px vs the spec's 96px hero max); SF Compact was rejected as non-redistributable. |
| HTTPS/TLS client | `esp_http_client` + **`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`** | Espressif ships Mozilla's CA roots and maintains them, so certificate rotation is not our problem — spec §8.2 names manual CA-bundle upkeep as a real cost of the MCU path. Pinning only Stripe's current root was rejected: it is smaller, but a root rotation would brick every device in the field until firmware is updated. |
| JSON parsing | **cJSON, full-buffer** (ships with ESP-IDF) | Spec §8.3 mandates a filtered streaming parse, assuming ~320KB of heap. This board reports **8MB PSRAM** (confirmed at Stage 1 boot), so a 300–400KB subscriptions page fits with enormous margin. Simpler code, which is what should be handling money figures. |

### Verified board configuration

Source: Waveshare repo, `examples/ESP32-S3-LCD-1.54-demo/ESP-IDF-5.5.1/05_lvgl_example/main/main.c` (the **non-touch** demo tree — confirmed to exist as a separate tree from the touch variant, closing an earlier research gap). Verified 2026-08-15.

| Signal | GPIO |
|---|---|
| SCLK | 38 |
| MOSI | 39 |
| RST | 40 |
| DC | 45 |
| CS | 21 |
| Backlight (BL) | 46 |

Other confirmed settings from the same source:
- SPI host: `SPI3_HOST`, pixel clock **40 MHz** (matches the spec's §5.3 SPI timing assumptions exactly)
- MISO / QuadWP / QuadHD: not connected (`GPIO_NUM_NC`)
- Panel driver call: `esp_lcd_new_panel_st7789()`, 16 bpp, `ESP_LCD_COLOR_SPACE_RGB`
- **`esp_lcd_panel_invert_color(panel, true)` is required** for this panel — easy to miss, causes inverted colors if omitted
- LVGL: `LV_COLOR_FORMAT_RGB565`, `swap_bytes = true`, `buff_dma = true`, draw buffer height 50 lines, double-buffered
- Backlight on level: `1` (active high)

Caveat: the non-touch demo's `idf_component.yml` still lists `espressif/esp_lcd_touch_cst816s` (apparent copy-paste from the touch variant) and its `main.c` still calls `app_touch_init()`. Both should be dropped for our board.

### Background deviation from spec §4.1 / §3.1 — measured on hardware 2026-08-15

**Spec says `#121211`, not pure black. On this panel that is backwards: use `#000000`.**

Spec §3.1 reasons that an always-on IPS backlight lifts `#000000` into "dark charcoal with visible edge bleed", so it picks `#121211` as the honest floor. This panel does the opposite. A near-black ramp (`main/colortest.c`, `COLORTEST_RAMP 1`) rendering `0x00 / 04 / 08 / 0C / 12 / 18 / 20 / 30` showed:

- `0x00` renders **genuinely black**
- `0x04` — four steps up — has **already jumped to a clearly visible slate gray**
- everything from `0x04` through `0x30` **collapses to roughly the same mid-gray**

The panel's response is close to a step function at the bottom: there is no "very dark but not black" state. `#121211` (luma ≈ 18) lands inside that collapsed band and renders mid-gray — much worse than the edge bleed §3.1 was trying to avoid.

Checked the rest of the palette against the collapsed band: only the background fell inside it. `COLOR_INACTIVE #3A3A37` (luma ≈ 58) sits above it and still reads as distinct for the unfilled rotation dots.

Re-run `main/colortest.c` if the panel, driver, or backlight setting ever changes.

### Accent color discipline (spec §4.2)

Spec §4.2 reserves green for realized gains and warns that "if everything is
green, green means nothing". Green had drifted onto screens that were merely
reporting a level (PAID SUBS decoratively, MRR unconditionally); both were
removed, and the remaining screens only turn green once they know the value is
actually a gain.

Red follows the same rule, and §4.2 keeps it out of the base palette entirely —
it is added "only for threshold breaches, so its appearance carries
information". It is used on **exactly one screen: FAILED**, the only screen that
is actionable rather than informational: money actively being lost to a declined
card, and recoverable if acted on. Cancellations are deliberately *not* red —
churn is ordinary business, not a breach.

`COLOR_RED` is `#E74D63`: 5.64:1 against the `#000000` field (clears WCAG AA),
and it round-trips through RGB565 **exactly**. That last property is a hard
requirement for accent colors here, not a nicety. The first candidate `#E0555F`
(5.63:1) renders as `#E7555A` — a 7/255 red shift, invisible to the eye but
enough that the constant in `layout.h` was not the color on the glass, and pixel
tests comparing against the constant failed. Stripe's own error red `#CD3D64`
was rejected separately at 4.45:1, below AA and muddy at distance.

`test_layout.c` pins all of this: red is outside the collapsed band, survives
RGB565 unchanged, and stays separated from `COLOR_AMBER` in the green channel so
a future edit cannot let it drift orange into the degraded-state color.
`test_screens.c` asserts red appears on the alert screen and on **no** other —
the check that keeps red from spreading the way green did.

**Diagnosing this took five attempts.** The earlier wrong turns — flipping `invert_color`, blaming a double byte-swap, blaming the LVGL theme — are recorded in the git history of `display.c`; the actual settings that are correct for this board are `invert_color(true)` and `swap_bytes = true`, matching Waveshare's example. Two config lines inherited from that example (`CONFIG_LV_COLOR_16_SWAP`, `CONFIG_LV_MEM_CUSTOM`) are LVGL 8 options that LVGL 9 silently ignores and have been removed. Note also that `sizeof(lv_color_t) == 3` is **normal** in LVGL 9 — it is always RGB888 at the API level, with `lv_color16_t` used in the draw buffer — so it is not evidence of a color-depth misconfiguration.

### Typeface deviation from spec §5.4 (decided 2026-08-15)

**The spec mandates monospace; this build uses Roboto Condensed Bold. Deliberate, tested on hardware.**

Spec §5.4 requires monospace so tabular figures stop numbers jittering when 94 becomes 100. On the real panel, Courier New was hard to read — a typewriter face with thin strokes, which is exactly what goes fuzzy on a 220 PPI backlit IPS panel. Candidates were rendered at true 240×240 and compared:

| Face | Kind | `$6.5k` width @64px | Tabular digits | Redistributable |
|---|---|---:|---|---|
| Courier New | mono | 192px | yes | no (Monotype) |
| Andale Mono | mono | 192px | yes | no |
| SF Mono | mono | 198px | yes | no (Apple) |
| SF Compact Bold | prop | 178px | no | no (Apple) |
| Helvetica Neue | prop | 158px | yes | no |
| **Roboto Condensed Bold** | **prop** | **146px** | **yes** | **yes (SIL OFL)** |

Why the deviation is safe:
- **Monospace wastes the column.** It reserves a full character cell for `.`, so glyphs shrink to compensate. A condensed proportional face renders *larger* letterforms in *less* width — directly serving §2.2, the legibility rule the whole device is built on.
- **Roboto Condensed has tabular figures anyway** (all digits advance 505/1000 em), so it keeps the anti-jitter property §5.4 wanted, while letters stay proportional. The rule's goal is met by a different means.
- **Left alignment already mitigates jitter.** §5.2 anchors values at a fixed left edge, and the hero swaps whole screens every 8s rather than ticking digits in place. A rendered jitter test (94→100, $6.5k→$11.1k) showed no objectionable shift in any candidate.

Because it is condensed, values size up a step versus wider faces — measured on hardware:

| Value | Monospace | SF Compact | **Roboto Condensed** |
|---|---:|---:|---:|
| `$6.5k` | 60px | 64px | **88px** (10.1mm) |
| `$145k` | — | 64px | **76px** |
| `$1.45M` | — | 52px | **64px** |

88px is "across the room" in the spec §2.2 table, where 64px is only "glanceable".

**Licensing:** an earlier iteration used SF Compact, which is an Apple system font and cannot be redistributed — a blocker given §9 contemplates selling this device. Roboto Condensed is SIL OFL; its license is vendored at `tools/fonts/LICENSE-RobotoCondensed.txt`.

Consequences (already implemented):
- `MONO_ADVANCE_EM` **removed** from `layout.h`. The spec's `len × size × 0.6em` width formula is invalid for a proportional face.
- `hero_size_for_length(size_t)` **replaced** by `hero_size_for_text(const char *)`. Width depends on *which* characters, not how many.
- `main/hero_size.c` carries a per-glyph advance table (0x20–0x7A) extracted from the vendored font. **If the face or weight changes, regenerate it with `tools/dump_advances.py`** or sizing will silently disagree with what LVGL renders.
- The font (`tools/fonts/RobotoCondensed-Bold.ttf`, a static `wght=700` instance of Google's variable original) is vendored so the build works offline.

Still outstanding: the generated font set is **2.9MB**, against the spec's ~20KB estimate (§5.4) — full ASCII at 4bpp, uncompressed. Fits comfortably in the 6MB partition, but trimming to the spec's actual glyph set (digits, currency, ~12 uppercase) would cut it by well over an order of magnitude.

**Open gaps: both closed at Stage 4** (see the toolchain table above). The TLS client uses Espressif's maintained CA bundle, and JSON is parsed full-buffer with cJSON — the spec's streaming requirement (§8.3) does not apply to a board with 8MB PSRAM.

### Input: no touch controller (measured 2026-08-15)

**Spec §1 principle 3 says "no interaction, no menus, no touch." This build adds tap-to-advance navigation — a deliberate departure.** Waiting out the 8-second interval to see one specific metric is a real annoyance in use. Rotation is never suspended, so the device remains the rotating instrument the spec describes; tapping only advances it early.

**This board has no touch controller.** An I2C scan (`GPIO41`/`GPIO42`) found five devices and nothing at `0x15` where a CST816S would answer:

| Address | Device |
|---|---|
| 0x13 | ES7210 audio ADC |
| 0x18 | ES8311 audio codec |
| 0x40 | power monitor |
| **0x6B** | **QMI8658 IMU** (WHO_AM_I 0x05, rev 0x7C) |

Waveshare's "Touch Version Options" refers to the `ESP32-S3-Touch-LCD-1.54` SKU, a different part number. Note the IMU answers at **0x6B, not 0x6A** — 0x6A appears in the scan but returns an invalid ID.

**Single tap, not double.** An earlier version required a double tap. Measurement killed it: one physical tap produces several impacts as the case rings (gaps of 20-60 ms), and a natural gap between two taps measured ~570 ms median, straddling any pairing window. 48 impacts produced 14 accepted gestures that did not match what the user did.

**Detection is HOST-SIDE, from raw accelerometer magnitude — not the chip's tap engine.**

The QMI8658 has an on-chip tap detector and it was implemented here first. It never fired: zero detections across many counted tests, for impacts up to 8604 mg. Everything observable was verified correct — CTRL1/2/7/8 all read back as written, both CTRL9 `CONFIGURE_TAP` commands ACKed within 20ms, every CAL parameter read back intact, the accelerometer was running, sync-sample mode was off, and widening the peak/tap windows changed nothing. **Cause never found** on this part (WHO_AM_I 0x05, revision 0x7C). The configuration is retained in `imu.c` in case someone revisits it.

**The shipped trade-off: reliable detection, with occasional false triggers.**

Threshold is **4000 mg** — the setting that detected **5 of 5** taps in a counted test. It admits roughly **one spurious advance per 20 seconds idle**.

That is not filtered out, because it cannot be without also rejecting real taps:

| Filter tried | False triggers | Real taps detected |
|---|---|---|
| none (shipped) | ~1 per 20s idle | **5 of 5** |
| require 2 consecutive elevated samples | 0 | **0 of 5** |
| reject saturated axes | 0 | **2 of 5** |

The reason is structural: this board's I2C bus intermittently returns corrupt reads that decode as large single-sample accelerations, and **a real tap also lands in exactly one 20ms sample**. The bus cannot be polled faster — 5-8ms tripped the task watchdog. Only the unambiguous corruption signature (all three axes reading identically) is filtered.

**A real bug was found and fixed along the way.** `TAP_STATUS` bits 6:4 are a **three-bit** axis field; the code masked it with `0x03`, truncating it. Combined with an "axis == 0 means noise" filter, that silently discarded genuine taps. Now decoded in `tapstatus.c` with 337 host tests, including one asserting the buggy mask would fail. A code comment claiming axis *priority* gates which axes are watched was also wrong — the engine triggers on the square sum of all three axes; priority is only a tie-break.

**If this needs to be better, use the PLUS button (GPIO4).** It is a debounced digital input with none of these failure modes, and Waveshare's own example for this SKU uses the three programmable buttons. Tap tuning cost roughly twenty hardware test rounds and landed on a compromise; the button would not.

**I2C bus speed matters here.** At 400kHz, reads failed constantly *while tapping* — striking the case disturbs the bus, and the failures dropped exactly the samples tap detection needs. At 100kHz the drop rate is under 1%. If tap detection ever becomes unreliable, check the drop rate in the periodic `imu:` health log before adjusting thresholds.

## Stage 3: WiFi + captive portal setup flow (State C)

Goal: self-contained subsystem, testable independent of Stripe connectivity.

- [x] AP mode boot, broadcast `Setup-XXXX` SSID (§9.1 step 1) — verified as `Setup-C561`
- [x] Captive portal for WiFi credential entry — **auto-opens on iOS** via the DNS responder; no IP typing needed
- [x] Store WiFi credentials in NVS
- [x] Render State C screen (Setup) with live SSID and firmware version in footer
- [x] **Full flow verified on hardware 2026-08-15**: portal auto-opened → credentials entered → validated → stored → restart → joined network → DHCP lease `192.168.68.67` → rotation resumed
- [x] Capture Stripe restricted-key input via the portal — moved to Stage 4 and **done there**: the key is validated the moment it is entered (§9.1 step 4) rather than stored untested
- [ ] Capture preferences: timezone, currency, rotation contents, brightness schedule (§9.1 step 5) — **parked** (see Parked below). Timezone is currently hardcoded to `EST5EDT,M3.2.0/2,M11.1.0/2`

### Notes from bring-up

- **WiFi authmode threshold is `OPEN`, deliberately.** The stack upgrades itself when a password is present (`authmode threshold changes from OPEN to WPA2` in the log), so open networks are still joinable while protected ones stay secure. Pinning WPA2 would reject legitimate open and guest networks.
- **The first association attempt often fails** and succeeds on retry — observed as `disconnected, retry 1/5` immediately before a successful join. The bounded retry (5 attempts, then State-B-style failure rather than silent looping) is load-bearing, not defensive padding.
- **NVS is unencrypted.** Deliberate per §9.1: the device holds a read-only restricted key, so a stolen device leaks a subscriber count, not the ability to move money. NVS encryption needs flash encryption with an eFuse-burned key — irreversible, and it complicates development flashing. Revisit before selling hardware.
- **The setup page never passes through `printf`.** Its inline CSS contains `%` characters that were read as format specifiers; the page is now sent in chunks. The compiler caught this, not testing.

## Stage 4: HTTPS client + Stripe auth validation

Goal: prove connectivity to the Stripe API and validate the restricted key; State B (auth error) becomes real here.

- [x] Resolve open gap: pick TLS client approach — `esp_http_client` with `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`
- [x] Implement `GET /v1/subscriptions?limit=1` validation call (§9.1 step 4) — `VALIDATE_URL`, `main/stripe_api.c:19`
- [ ] On success: show live MRR on the setup screen before completing setup (the "converts skeptics" moment, §9.1 step 4) — **not done.** The key validates and the portal confirms success, but the setup screen never shows a live number before handoff. Cosmetic, and only visible during first-run setup
- [x] On failure (401 or other): trigger State B (Auth Error), re-enter setup mode rather than silently failing
- [x] Render State B screen with plain-language message + error code in footer — verified on hardware

## Stage 5: MRR computation engine

Goal: standalone, unit-testable function against fixture JSON — no live API or display dependency yet.

- [x] Resolve open gap: JSON parsing — full-buffer cJSON in PSRAM, given the confirmed 8MB
- [x] Implement §7.2 algorithm: iterate subscriptions/items, skip non-recurring prices, convert interval (month/year/week/day) to monthly value
- [x] Apply discounts before summing (§7.2 step 1 / §10 checklist) — `mrr_apply_discount()`, handles both `percent_off` and `amount_off`
- [x] Exclude trials from MRR total, count separately (§7.2 step 2) — `main/mrr.c:94`, "counted, never summed"
- [x] Detect and flag tiered/metered pricing rather than silently miscomputing (§7.2 step 3) — `has_tiered` / `tiered_count`
- [x] Assert single-currency, handle/reject mixed-currency data (§7.2 step 4) — `mixed_currency` flag; the device has no rate table, so it refuses rather than guessing
- [x] Unit test against fixture JSON covering all cases — `test_mrr` (58 checks), `test_stripe_parse` (48 checks)
- [x] **Verified against the live account**: computed MRR matched the Stripe dashboard at $941.33 across 28 subscriptions. Carried into fixtures as a regression constant (`test_mrr.c:416`), though the comparison itself was made by eye against the dashboard and is not reproducible from the repo

## Stage 6: Full polling layer

Goal: hybrid polling strategy live, reliability requirements in place, State A (stale) becomes real.

- [x] Full recompute every 10 minutes and on boot (§7.3) — `REFRESH_INTERVAL_MS`, `main/main.c:677`
- [x] Incremental update every 60s via `GET /v1/events`, adjusting cached state (§7.3) — `EVENTS_INTERVAL_MS`, `main/main.c:678`
- [x] ~~Populate Last Event screen from incremental event data~~ — screen **removed** by decision: it only ever read "changed", which is not worth 5 seconds of attention. Replaced by the CANCELLED screen
- [x] ~~Hourly full reconciliation regardless of incremental state~~ — **skipped by decision.** The spec asks for a third cadence to correct incremental drift, but the 10-minute full recompute already rebuilds from scratch six times an hour, which subsumes it. A separate hourly pass would be redundant work against the API
- [x] Persist last-good state to flash; render cached values immediately on boot (§7.4 step 1) — `main/cache.c`, with version checking and plausibility validation. Removed the ~12s blank period at boot
- [x] Exponential backoff on failure: 60s, 120s, 240s, capped at 15 min (§7.4 step 3) — `freshness_retry_delay_ms()`, `main/freshness.c:56-62`
- [x] NTP sync with explicit local timezone offset (§7.4 step 4)
- [x] Stale threshold at 15 minutes triggers State A (§7.4 step 2)
- [x] Render State A screen: values dim to muted, age shown in amber, retry status in footer — verified on hardware

## Stage 7: Screen rotation + integration

Goal: tie polling → cached state → renderer into the full rotation loop across all screens.

- [x] Rotation timer with dots reflecting position (§6.1) — **10 screens defined at 5s**, against the spec's 6 at 8s. The deck grew (ARR, ARPU, CANCELLED, FAILED) and the interval was tuned down by eye: 8s → 6s → 5s. Screens are grouped by kind — revenue, alert, movement, composition (`main/rotation.c:81-114`). TRIALS and CONVERSION are defined but never shown on this account, which has no trials
- [x] Conditional screens: only enter rotation when they have something to say (§6.1) — visibility rules in `rotation_build()` (`main/rotation.c:72`), driven from `rebuild_rotation()` (`main/main.c:146`)
- [x] Full integration: live Stripe data flowing through polling → MRR engine → cached state → rendering, no hardcoded fixtures remaining
- [x] Manual advance on the leftmost button (GPIO0) — added by request; not in the spec
- [x] Accent color discipline (§4.2) — green for realized gains only, red on FAILED only
- [x] Verify state transitions (normal → stale → auth error → setup) all correctly interrupt/resume rotation — covered by `test_state` (29 checks): all 8 flag combinations exhaustively, both precedence rules, full transition sequences including recovery, and the rule that stale keeps rotating while setup and auth-error take the screen over.

  Writing the test required extracting the decision first. The precedence chain was welded into `show_current()` in `main.c`, tangled with LVGL locking and ESP-IDF globals, so nothing host-side could reach it — a test against anything else would have been theater. `main/state.c` now holds it as a pure function over three flags and `main.c` calls it, so the test guards the path the device actually runs.

  Mutation-checked rather than trusted: inverting the auth/stale precedence produces 3 failures, including the exact §6.2 case. Rotation re-confirmed on hardware after the refactor — 8 screens wrapping `[8/8] PAID SUBS` → `[1/8] MRR` at 5s.

## Stage 8: Power management

Goal: cosmetic/lifetime feature, bolted on once the rest is stable.

**Skipped by decision.** Not wanted for a desk device that is off when the room is empty.

- [ ] ~~PWM night-dimming schedule (e.g. 20% brightness 10pm–7am) (§3.3)~~
- [ ] ~~Confirm dimming doesn't visually break legibility floor at reduced brightness~~

---

## Parked

Not blocking. Revisit only if this becomes a product rather than one desk device.

### Would need doing before a second user

- [ ] **Setup preferences via the portal** (§9.1 step 5) — timezone, currency, rotation contents. Timezone is hardcoded to `EST5EDT,M3.2.0/2,M11.1.0/2`, which is correct here and wrong everywhere else. This is the one parked item that is a genuine defect for anyone but the current owner
- [ ] **NVS encryption** — currently unencrypted by deliberate decision (a read-only restricted key leaks a subscriber count, not the ability to move money). Needs flash encryption with an eFuse-burned key: irreversible, and it complicates development flashing

### Performance polish

- [ ] **Partial-window updates (§5.3)** — repaint only the ~240x110 middle block via ST7789 CASET/RASET instead of the full 240x240. The spec's case: 53KB/~11ms versus 115KB/~25ms, where 25ms can read as a visible flicker.

  Low value here, and worth measuring before implementing. The spec itself says full redraws are fine on rotation transitions, where a wipe reads as intentional — and rotation is nearly all of what this device does. The optimization only helps in-place updates, which happen at most once a minute and usually change nothing on screen. LVGL also does its own dirty-region tracking, so some of this may already be happening.

- [ ] Live MRR on the setup screen before completing setup (§9.1 step 4) — the "converts skeptics" moment; only ever seen during first-run setup

### Commercial checklist (spec §10)

- [ ] Printed permission card in box, with QR code to setup guide
- [ ] Firmware source published — the repo is currently **private**; this is a decision, not a task
- [ ] "Not affiliated with Stripe" disclosure in listing
- [ ] Key rotation reminder documented for the customer
- [ ] MRR definition documented, one page, shipped in the box
