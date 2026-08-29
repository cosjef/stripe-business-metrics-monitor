/*
 * Stripe Revenue Display -- Arduino port, stage 2: the real deck.
 *
 * Stage 1 proved the panel and that LVGL frames reach the glass repeatedly.
 * This stage brings across the rendering core from the ESP-IDF tree --
 * screens.c, the C6 layout, the hero ladder and the generated fonts -- and
 * rotates the deck on fixture values. Those files are compiled from
 * firmware/main/ rather than copied (see platformio.ini), so the host suite
 * in firmware/test keeps covering exactly the code that runs here.
 *
 * Values below are fixtures, deliberately. The device must not invent numbers
 * (spec 1, principle 4), and there is no network yet; these are stand-ins for
 * layout verification only and are replaced wholesale in stage 4.
 *
 * Stage plan:
 *   1. [done] colour probe + LVGL counter -- panel and LVGL proven
 *   2. [this file] the deck, rotating on fixtures
 *   3. WiFi, NVS settings, captive portal
 *   4. Stripe fetch with the streaming JSON parser
 */
#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"

extern "C" {
#include "screens.h"
}
#include "layout.h"

static XPowersPMU s_pmu;

/*
 * Bring up the PMIC and pulse the panel reset.
 *
 * The AXP2101 gates the panel rails, so this must complete before any QSPI
 * traffic. ALDO3 doubles as the CO5300's reset line -- the panel has no reset
 * GPIO -- so cycling that rail HIGH/LOW/HIGH with 100ms holds is the reset.
 * Skipping the pulse leaves the panel in an indeterminate state and the screen
 * black while QSPI init and brightness writes both still succeed.
 */
static bool power_up(void)
{
    Wire.begin(IIC_SDA, IIC_SCL);

    if (!s_pmu.begin(Wire, AXP2101_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("FAIL: AXP2101 not responding at 0x34");
        return false;
    }

    s_pmu.setALDO1Voltage(3300);
    s_pmu.setALDO2Voltage(3300);
    s_pmu.setALDO3Voltage(3300);
    s_pmu.setALDO4Voltage(3300);

    s_pmu.enableALDO1();
    s_pmu.enableALDO2();
    s_pmu.enableALDO4();

    /* ALDO3 is the panel reset. */
    s_pmu.enableALDO3();
    delay(100);
    s_pmu.disableALDO3();
    delay(100);
    s_pmu.enableALDO3();
    delay(100);

    s_pmu.enableBattDetection();
    s_pmu.enableBattVoltageMeasure();

    Serial.println("OK: AXP2101 rails up, ALDO3 reset pulsed");
    return true;
}

/*
 * The rotation deck, as fixtures.
 *
 * Ordering and colour rules follow spec 4.2 and are not cosmetic: green means
 * realized positive movement and nothing else, red is reserved for FAILED --
 * the one screen that is both a threshold breach and actionable. Everything
 * else stays in the neutral palette.
 */
static const screen_data_t s_deck[] = {
    { "MRR",             "$1,106.33", "33 active",        false, false, false, 0, 0 },
    { "NEW PAID",        "7",         "this month",       true,  false, false, 0, 0 },
    { "PAID SUBS",       "33",        "active",           false, false, false, 0, 0 },
    { "TRIALS",          "4",         "2 converting",     false, false, false, 0, 0 },
    { "CONVERSION",      "62%",       "trial to paid",    false, false, false, 0, 0 },
    { "ANNUAL RUN RATE", "$13,276",   "at current MRR",   false, false, false, 0, 0 },
    { "ARPU",            "$33.53",    "per subscriber",   false, false, false, 0, 0 },
    { "NET 30D",         "+$212.40",  "vs last month",    true,  false, true,  0, 0 },
    { "FAILED",          "$89.00",    "2 payments",       false, true,  false, 0, 0 },
};
static const int DECK_COUNT = sizeof(s_deck) / sizeof(s_deck[0]);

#define ROTATE_MS 5000

static void show(int index)
{
    /* Copy so the dot fields can be filled in without mutating the fixtures. */
    screen_data_t d = s_deck[index];
    d.dot_index = index;
    d.dot_count = DECK_COUNT;

    screen_draw_rotation(lv_screen_active(), &d);

    Serial.printf("[%d/%d] %s: %s\n", index + 1, DECK_COUNT, d.label, d.hero);
}

void setup(void)
{
    Serial.begin(115200);
    delay(2000);           /* let USB-CDC enumerate before the first print */

    Serial.println();
    Serial.println("=== Stripe Revenue Display -- Arduino port, stage 2 ===");
    Serial.println(BOARD_NAME);

    if (!power_up()) {
        Serial.println("HALT: no PMIC, panel cannot be powered");
        return;
    }

    if (!display_init()) {
        Serial.println("HALT: display init failed");
        return;
    }
    Serial.println("OK: CO5300 initialised");

    if (!display_lvgl_init()) {
        Serial.println("HALT: LVGL init failed");
        return;
    }
    Serial.println("OK: LVGL bound to panel");

    /* Confirms the shared layout header picked the C6 geometry. If this prints
     * the S3 numbers, hero_size.c's static assertions should already have
     * failed the build -- but print it anyway, because the whole class of bug
     * here is "renders happily at the wrong size". */
    Serial.printf("layout: panel %dpx, pad %d, hero %d-%dpx\n",
                  PANEL_PX, PAD_PX, SIZE_HERO_MIN, SIZE_HERO_MAX);

    show(0);
    Serial.println("stage 2 running: deck should rotate every 5s");
}

void loop(void)
{
    static uint32_t last_tick;
    static uint32_t last_rotate;
    static int index;

    const uint32_t now = millis();

    /* LVGL needs both a tick source and regular handler calls. */
    lv_tick_inc(now - last_tick);
    last_tick = now;
    lv_timer_handler();

    if (now - last_rotate >= ROTATE_MS) {
        last_rotate = now;
        index = (index + 1) % DECK_COUNT;
        show(index);
    }

    delay(5);
}
