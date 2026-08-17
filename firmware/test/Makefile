# Host tests. No hardware required.
#
#   make          build and run everything
#   make quick    logic tests only (no LVGL build)
#   make clean
#
# The screen tests link real LVGL and render into an offscreen framebuffer, so
# they need managed_components/ to be present. Run `idf.py reconfigure` in
# firmware/ first if it is missing.

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0

LVGL    := ../managed_components/lvgl__lvgl

# LVGL for the host: everything except other vendors' GPU backends, platform
# drivers, and the heavyweight optional libs.
LV_SRCS := $(shell find $(LVGL)/src -name '*.c' \
             -not -path '*/draw/nxp/*' -not -path '*/draw/renesas/*' \
             -not -path '*/draw/espressif/*' -not -path '*/draw/vg_lite*' \
             -not -path '*/draw/nema*' -not -path '*/draw/dma2d*' \
             -not -path '*/draw/opengles*' -not -path '*/drivers/*' \
             -not -path '*/libs/gltf/*' -not -path '*/libs/vg_lite_driver/*' \
             -not -path '*/libs/frogfs/*' 2>/dev/null)

FONT_SRCS := $(wildcard ../main/fonts/stripe_sans_*.c) ../main/fonts/fonts.c

# LVGL and the generated fonts are third-party or machine-written; -Werror on
# our own code, quiet for theirs.
LV_CFLAGS := -std=c11 -w -O0 -I . -I ../main -I ../main/fonts \
             -I $(LVGL) -I $(LVGL)/src -DLV_CONF_INCLUDE_SIMPLE

DEPS := ../main/hero_size.h ../main/layout.h ../main/baseline.h

CJSON := $(HOME)/esp/esp-idf/components/json/cJSON
LOGIC_BINS  := test_hero_size test_font_coverage test_layout test_tapdetect test_tapstatus test_provision test_stripe_key test_mrr test_format test_freshness test_events test_rotation test_cache test_state test_battery test_orientation
SCREEN_BIN  := test_screens
BINS        := $(LOGIC_BINS) test_stripe_parse $(SCREEN_BIN)

.PHONY: all test quick clean

all: test

test_hero_size: test_hero_size.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_hero_size.c ../main/hero_size.c

test_font_coverage: test_font_coverage.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_font_coverage.c ../main/hero_size.c

test_layout: test_layout.c ../main/hero_size.c ../main/baseline.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_layout.c ../main/hero_size.c ../main/baseline.c

# Built as one unit: LVGL's headers require lv_conf.h on the include path, and
# the generated fonts are large enough that per-object rules would not pay off.
test_screens: test_screens.c harness.c ../main/screens.c ../main/hero_size.c \
              ../main/baseline.c $(FONT_SRCS) lv_conf.h $(DEPS)
	@if [ -z "$(LV_SRCS)" ]; then \
	  echo "error: LVGL not found at $(LVGL)"; \
	  echo "run 'idf.py reconfigure' in firmware/ to fetch components"; \
	  exit 1; \
	fi
	$(CC) $(LV_CFLAGS) -o $@ \
	  test_screens.c harness.c ../main/screens.c ../main/hero_size.c \
	  ../main/baseline.c $(FONT_SRCS) $(LV_SRCS)

quick: $(LOGIC_BINS)
	@fail=0; for t in $(LOGIC_BINS); do ./$$t || fail=1; echo; done; exit $$fail

test: $(BINS)
	@fail=0; for t in $(BINS); do ./$$t || fail=1; echo; done; exit $$fail

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

test_tapdetect: test_tapdetect.c ../main/tapdetect.c ../main/tapdetect.h
	$(CC) $(CFLAGS) -o $@ test_tapdetect.c ../main/tapdetect.c

test_tapstatus: test_tapstatus.c ../main/tapstatus.c ../main/tapstatus.h
	$(CC) $(CFLAGS) -o $@ test_tapstatus.c ../main/tapstatus.c

test_provision: test_provision.c ../main/provision.c ../main/provision.h
	$(CC) $(CFLAGS) -o $@ test_provision.c ../main/provision.c

test_stripe_key: test_stripe_key.c ../main/stripe_key.c ../main/stripe_key.h
	$(CC) $(CFLAGS) -o $@ test_stripe_key.c ../main/stripe_key.c

test_mrr: test_mrr.c ../main/mrr.c ../main/mrr.h
	$(CC) $(CFLAGS) -o $@ test_mrr.c ../main/mrr.c

test_format: test_format.c ../main/format.c ../main/hero_size.c
	$(CC) $(CFLAGS) -o $@ test_format.c ../main/format.c ../main/hero_size.c

test_stripe_parse: test_stripe_parse.c ../main/stripe_parse.c ../main/mrr.c
	$(CC) -std=c11 -w -O0 -I ../main -I $(CJSON) -o $@ \
	  test_stripe_parse.c ../main/stripe_parse.c ../main/mrr.c $(CJSON)/cJSON.c

test_freshness: test_freshness.c ../main/freshness.c ../main/freshness.h
	$(CC) $(CFLAGS) -o $@ test_freshness.c ../main/freshness.c

test_events: test_events.c ../main/events.c ../main/events.h
	$(CC) $(CFLAGS) -o $@ test_events.c ../main/events.c

test_rotation: test_rotation.c ../main/rotation.c ../main/rotation.h
	$(CC) $(CFLAGS) -o $@ test_rotation.c ../main/rotation.c

test_cache: test_cache.c ../main/cache.c ../main/cache.h
	$(CC) $(CFLAGS) -o $@ test_cache.c ../main/cache.c

test_state: test_state.c ../main/state.c ../main/state.h
	$(CC) $(CFLAGS) -o $@ test_state.c ../main/state.c

test_battery: test_battery.c ../main/battery.c ../main/battery.h
	$(CC) $(CFLAGS) -o $@ test_battery.c ../main/battery.c

test_orientation: test_orientation.c ../main/orientation.c ../main/orientation.h
	$(CC) $(CFLAGS) -o $@ test_orientation.c ../main/orientation.c
