# Firmware Build Plan

Tracks implementation progress against [stripe-revenue-display-spec.md](stripe-revenue-display-spec.md). Update checkboxes as work lands; add dated notes under a stage when a decision changes or a gap is closed.

---

## Toolchain decisions

| Area | Choice | Rationale |
|---|---|---|
| Hardware | Waveshare ESP32-S3-LCD-1.54 (non-touch), ST7789, 240x240, SPI, 8MB PSRAM, 16MB flash | Confirmed via product research 2026-08-15. PSRAM headroom means the spec's "must stream-parse, 300KB won't fit in heap" concern (§8.3) is likely avoidable — full-buffer JSON parsing may be viable. Revisit at Stage 4/5. |
| Framework | ESP-IDF (not Arduino) | User choice. More setup than Arduino but no HAL abstraction tax. |
| Graphics | `esp_lcd` (`esp_lcd_panel_st7789`) + `esp_lvgl_port` + LVGL 9 | This is what Waveshare's own ESP-IDF example for this exact board uses. Confirmed 2026-08-15 via Waveshare's repo (`examples/ESP32-S3-LCD-1.54-demo/ESP-IDF-5.5.1/05_lvgl_example`). Rejected LovyanGFX for IDF — technically possible but thinly documented outside Arduino, not what Waveshare ships. |
| Starting point | Waveshare `ESP32-S3-Touch-LCD-1.54` GitHub repo, `05_lvgl_example` | Drop the `esp_lcd_touch_cst816s` dependency (touch component, not present on our non-touch board). |
| Fonts | **Roboto Condensed Bold (SIL OFL)**, generated to LVGL bitmap fonts at 12 sizes (18/20/22 UI + 24/32/40/52/60/64/76/88/96 hero) via `tools/gen_fonts.sh` | See "Typeface deviation from spec §5.4" below. LVGL's stock Montserrat was rejected (caps at 48px vs the spec's 96px hero max); SF Compact was rejected as non-redistributable. |
| HTTPS/TLS client | **Not yet decided** | Open gap — see below. |
| JSON parsing | **Not yet decided** | Open gap — see below. |

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

**Open gaps (flag before Stage 4):**
- TLS client library for calling the Stripe API from ESP-IDF (e.g. `esp-tls` / `esp_http_client` native to IDF vs. something else). No board-specific issues surfaced in research so far, but unverified for this exact pairing.
- JSON parsing approach: **likely resolved in favor of full-buffer parsing.** Boot log confirms `esp_psram: Found 8MB PSRAM device` and `Adding pool of 8192K of PSRAM memory to heap allocator` — a 300-400KB response fits with enormous margin, so the spec's §8.3 streaming-parse requirement (written assuming ~320KB total heap) does not apply to this board. Confirm with a real payload at Stage 5 before closing.

---

## Stage 0: Environment (done 2026-08-15)

- [x] Board enumerates over USB — `/dev/cu.usbmodem3101` (ESP32-S3 native USB, no CP210x/CH340 driver needed)
- [x] ESP-IDF v5.5.1 cloned to `~/esp/esp-idf`, toolchain installed for `esp32s3` (satisfies Waveshare's `idf: '>=5.4'`)
- [x] Xtensa compiler verified: `xtensa-esp-elf-gcc 14.2.0`
- [x] `cmake` / `ninja` installed via Homebrew (ESP-IDF's macOS installer does not bundle these)
- [x] Firmware project scaffolded at `firmware/`

## Stage 1: Display hello-world

Goal: prove the hardware path (SPI, ST7789 driver, panel init) works, and the three-zone skeleton renders correctly with hardcoded data.

- [x] Pin definitions captured from Waveshare's non-touch demo into `main/board_config.h`
- [x] Spec constants (baselines, palette, size floors) captured into `main/layout.h`
- [x] Hero auto-sizing (§2.4) implemented in `main/hero_size.c` as `hero_size_for_text()` — per-glyph width measurement, real screen-deck values, the `$145k` overflow case, monotonicity, unknown-glyph fallback
- [x] Baseline positioning extracted to `main/baseline.c` and tested (LVGL positions by top-left, the spec by baseline)
- [x] Monospace→Roboto Condensed typeface change (see deviation section above), fonts generated and flashed
- [x] **177 host checks passing across three suites** (`cd firmware/test && make`). Coverage of host-testable modules: 94.5% line, 100% function
- [x] Minimal `esp_lcd` + `esp_lvgl_port` + LVGL project written (`main/display.c`, `main/main.c`), touch dependency dropped
- [x] **Project builds** (`idf.py build`) — 1.07MB binary, 83% of app partition free
- [x] Flashed to board, boots clean with no panics; display init sequence confirmed in log (SPI → panel IO → ST7789 → `display ready: 240x240`), LVGL task running
- [x] Baseline positioning uses real font metrics (`line_height - base_line`) rather than approximating; fixed a bug where one shared `static lv_style_t` was overwritten by every label
- [x] Colors correct on glass — required `invert_color(true)` **and** removing the double byte-swap (`swap_bytes` was set in both `sdkconfig` and the LVGL port config, cancelling out and transposing red/blue)
- [x] **Render the three-zone skeleton (§5.1) — visually confirmed on hardware 2026-08-15.** Label at y=16, hero baseline y=150, subtitle baseline y=178, six rotation dots at y=214 with index 0 filled. Black field, Roboto Condensed Bold in warm off-white, green subtitle. All elements correct.
- [x] Fixed invisible rotation dots — `lv_obj_remove_style_all()` resets `bg_opa` to transparent, so the dots were being drawn but not painted
- [ ] Confirm partial-window update path (CASET/RASET, §5.3) works — measure actual redraw time, compare to spec's ~11ms partial / ~25ms full estimate
- [ ] Confirm 16px padding / 208px usable column matches spec measurements on real hardware
- [ ] **Trim the font set.** Currently 2.9MB (full ASCII 0x20–0x7A at 4bpp, uncompressed) vs the spec's ~20KB estimate (§5.4). Restrict to the glyphs the screen deck actually uses — digits, `$.,%+-kMK`, and the ~12 uppercase letters in the labels — and re-enable compression. Do this **before Stage 3**, when WiFi + TLS + CA bundles start competing for flash. Note: changing the glyph range means regenerating the advance table via `tools/dump_advances.py`.

## Testing status

`cd firmware/test && make` — **177 checks across three suites, all passing.**

| Suite | Covers |
|---|---|
| `test_hero_size` | per-glyph width measurement, 208px column constraint, large-account overflow, monotonicity, unknown-glyph fallback |
| `test_font_coverage` | every size `hero_size_for_text()` can return has a generated font that declares its symbol |
| `test_layout` | baseline→top-left conversion, spec baselines on-screen at all sizes, font-lookup coverage, palette renderability and contrast, layout geometry invariants |

| Module | Coverage |
|---|---|
| `main/hero_size.c` | 88.6% line, 100% function |
| `main/baseline.c` | 100% line, 100% function |
| **host-testable total** | **94.5% line, 100% function** |
| `main/display.c`, `main/main.c` drawing, `main/fonts/fonts.c` | **0% — not host-testable** |

**Known gap.** Hardware-coupled code (SPI setup, ST7789 init, LVGL wiring, drawing calls) has no automated test. Every visual bug in Stage 1 — inverted colors, the LVGL theme painting over the background, invisible rotation dots — was caught by photographing the screen, not by a test. The palette tests in `test_layout` now catch the *class* of bug behind the background failure, but not rendering itself.

Closing this needs an **LVGL host harness**: build LVGL for the host, render a screen into a memory buffer, and assert on pixels (background color, text position, dot visibility). Worth doing before or early in Stage 2 — nine screens verified by photograph does not scale, and Stage 2 is where the screen count multiplies.

## Stage 2: Bitmap font + layout engine

Goal: pure rendering logic, testable without any network code, producing all 9 static screen mockups from hardcoded fixture data.

- [x] Hero-size auto-computation — done in Stage 1 as `hero_size_for_text()`. Note this no longer uses the spec's `len × 0.6em` formula; see the typeface deviation section.
- [x] 208px column overflow check — done in Stage 1 as `text_fits()`, measuring real glyph advances
- [ ] ~~Negative letter-spacing (~-0.02em) on hero values~~ — **dropped.** Spec §5.4 recommends this to pull *monospace* digits toward a proportional appearance; Roboto Condensed is already proportional and condensed, so it no longer applies. Revisit only if the hero looks loose on glass.
- [ ] Render all 6 rotation screens (MRR, New Paid, Paid Subs, Trials, Conversion, Last Event) from hardcoded fixture values, compare visually against `01-mrr.png` and the spec's other screen mockups
- [ ] Render all 3 state screens (Stale, Auth Error, Setup) from hardcoded fixture values
- [x] Typeface decision resolved — Roboto Condensed Bold, generated at all 12 spec sizes (see deviation section)

## Stage 3: WiFi + captive portal setup flow (State C)

Goal: self-contained subsystem, testable independent of Stripe connectivity.

- [ ] AP mode boot, broadcast `Setup-XXXX` SSID (§9.1 step 1)
- [ ] Captive portal for WiFi credential entry
- [ ] Store WiFi credentials in NVS
- [ ] Capture Stripe restricted-key input via the portal
- [ ] Capture preferences: timezone, currency, rotation contents, brightness schedule (§9.1 step 5)
- [ ] Render State C screen (Setup) with live SSID and firmware version in footer

## Stage 4: HTTPS client + Stripe auth validation

Goal: prove connectivity to the Stripe API and validate the restricted key; State B (auth error) becomes real here.

- [ ] Resolve open gap: pick TLS client approach for ESP-IDF
- [ ] Implement `GET /v1/subscriptions?limit=1` validation call (§9.1 step 4)
- [ ] On success: show live MRR on the setup screen before completing setup (the "converts skeptics" moment, §9.1 step 4)
- [ ] On failure (401 or other): trigger State B (Auth Error), re-enter setup mode rather than silently failing
- [ ] Render State B screen with plain-language message + error code in footer

## Stage 5: MRR computation engine

Goal: standalone, unit-testable function against fixture JSON — no live API or display dependency yet.

- [ ] Resolve open gap: pick JSON parsing approach (full-buffer vs. streaming filter) given confirmed 8MB PSRAM
- [ ] Implement §7.2 algorithm: iterate subscriptions/items, skip non-recurring prices, convert interval (month/year/week/day) to monthly value
- [ ] Apply discounts before summing (§7.2 step 1 / §10 checklist)
- [ ] Exclude trials from MRR total, count separately for the Trials screen (§7.2 step 2)
- [ ] Detect and flag tiered/metered pricing (`billing_scheme == "tiered"`) rather than silently miscomputing (§7.2 step 3)
- [ ] Assert single-currency, handle/reject mixed-currency data (§7.2 step 4)
- [ ] Unit test against fixture JSON covering: flat monthly, flat annual, discounted, trialing, tiered, mixed-currency cases

## Stage 6: Full polling layer

Goal: hybrid polling strategy live, reliability requirements in place, State A (stale) becomes real.

- [ ] Full recompute every 10 minutes, on boot, and on reconciliation trigger (§7.3)
- [ ] Incremental update every 60s via `GET /v1/events?limit=10`, adjusting cached MRR on `customer.subscription.{created,updated,deleted}` (§7.3)
- [ ] Populate Last Event screen from incremental event data
- [ ] Hourly full reconciliation regardless of incremental state (§7.3, drift correction)
- [ ] Persist last-good state to flash; render cached values immediately on boot, then refresh (§7.4 step 1)
- [ ] Exponential backoff on failure: 60s, 120s, 240s, capped at 15 min (§7.4 step 3)
- [ ] NTP sync with explicit local timezone offset (§7.4 step 4)
- [ ] Stale threshold at 15 minutes triggers State A (§7.4 step 2)
- [ ] Render State A screen: values dim to muted, age shown in amber, retry status in footer

## Stage 7: Screen rotation + integration

Goal: tie polling → cached state → renderer into the full 8-second rotation loop across all screens.

- [ ] Rotation timer cycling all 6 metric screens every 8s, dots reflecting position (§6.1)
- [ ] Conditional churn screen: only enters rotation when nonzero for the period (§6.1)
- [ ] Full integration: live Stripe data flowing through polling → MRR engine → cached state → rendering, no hardcoded fixtures remaining
- [ ] Verify state transitions (normal → stale → auth error → setup) all correctly interrupt/resume rotation

## Stage 8: Power management

Goal: cosmetic/lifetime feature, bolted on once the rest is stable.

- [ ] PWM night-dimming schedule (e.g. 20% brightness 10pm–7am) (§3.3)
- [ ] Confirm dimming doesn't visually break legibility floor at reduced brightness

---

## Commercial / non-firmware checklist (from spec §10, tracked here for completeness)

- [ ] Printed permission card in box, with QR code to setup guide
- [ ] Firmware source published
- [ ] "Not affiliated with Stripe" disclosure in listing
- [ ] Key rotation reminder documented for the customer
- [ ] MRR definition documented, one page, shipped in the box
