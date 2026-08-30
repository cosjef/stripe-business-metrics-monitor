# Host tests. No hardware required.
#
#   make          build and run everything
#   make quick    logic tests only (no LVGL build)
#   make clean
#
# The screen tests link real LVGL and render into an offscreen framebuffer, so
# they need the LVGL source. It arrives with the firmware's dependencies:
# run `pio pkg install` in firmware-c6/ first if it is missing.

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0 -I ../include

LVGL    := ../../firmware-c6/.pio/libdeps/c6_amoled/lvgl

# LVGL for the host: everything except other vendors' GPU backends, platform
# drivers, and the heavyweight optional libs.
LV_SRCS := $(shell find $(LVGL)/src -name '*.c' \
             -not -path '*/draw/nxp/*' -not -path '*/draw/renesas/*' \
             -not -path '*/draw/espressif/*' -not -path '*/draw/vg_lite*' \
             -not -path '*/draw/nema*' -not -path '*/draw/dma2d*' \
             -not -path '*/draw/opengles*' -not -path '*/drivers/*' \
             -not -path '*/libs/gltf/*' -not -path '*/libs/vg_lite_driver/*' \
             -not -path '*/libs/frogfs/*' 2>/dev/null)

FONT_SRCS := $(wildcard ../fonts/stripe_sans_*.c) ../fonts/fonts.c

# LVGL and the generated fonts are third-party or machine-written; -Werror on
# our own code, quiet for theirs.
LV_CFLAGS := -std=c11 -w -O0 -I . -I ../include -I ../fonts \
             -I $(LVGL) -I $(LVGL)/src -DLV_CONF_INCLUDE_SIMPLE

DEPS := ../include/hero_size.h ../include/layout.h ../include/baseline.h

LOGIC_BINS  := test_hero_size test_font_coverage test_layout test_tapdetect test_tapstatus test_provision test_stripe_key test_mrr test_format test_freshness test_events test_rotation test_cache test_state test_battery test_orientation test_geometry test_layout_c6 test_history test_sparkline test_jsonstream test_wifi_retry test_invoices test_fetch_result test_refresh test_timezone
SCREEN_BIN  := test_screens
BINS        := $(LOGIC_BINS) $(SCREEN_BIN)

.PHONY: all test quick clean

all: test

test_hero_size: test_hero_size.c ../src/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_hero_size.c ../src/hero_size.c

test_font_coverage: test_font_coverage.c ../src/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_font_coverage.c ../src/hero_size.c

test_layout: test_layout.c ../src/hero_size.c ../src/baseline.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ test_layout.c ../src/hero_size.c ../src/baseline.c

# Built as one unit: LVGL's headers require lv_conf.h on the include path, and
# the generated fonts are large enough that per-object rules would not pay off.
test_screens: test_screens.c harness.c ../src/screens.c ../src/hero_size.c \
              ../src/baseline.c $(FONT_SRCS) lv_conf.h $(DEPS)
	@if [ -z "$(LV_SRCS)" ]; then \
	  echo "error: LVGL not found at $(LVGL)"; \
	  echo "run 'pio pkg install' in firmware-c6/ to fetch it"; \
	  exit 1; \
	fi
	$(CC) $(LV_CFLAGS) -o $@ \
	  test_screens.c harness.c ../src/screens.c ../src/hero_size.c \
	  ../src/baseline.c $(FONT_SRCS) $(LV_SRCS)

test_fetch_result: test_fetch_result.cpp ../src/fetch_result.cpp \
                   ../../firmware-c6/src/stripe_fetch.h
	$(CXX) $(CXXFLAGS) -I../../firmware-c6/src -I../include -o $@ \
	    test_fetch_result.cpp ../src/fetch_result.cpp

test_timezone: test_timezone.c ../src/timezone.c ../include/timezone.h
	$(CC) $(CFLAGS) -I../include -o $@ test_timezone.c ../src/timezone.c

test_refresh: test_refresh.c ../src/refresh.c ../include/refresh.h
	$(CC) $(CFLAGS) -I../include -o $@ test_refresh.c ../src/refresh.c

test_invoices: test_invoices.c ../src/invoices.c ../include/invoices.h
	$(CC) $(CFLAGS) -o $@ test_invoices.c ../src/invoices.c

# Screenshots for the README. Drives the real renderers at 480x480 with the
# real fonts, so the images cannot drift from the code.
render_docs: render_docs.c harness.c ../src/screens.c ../src/hero_size.c \
             ../src/baseline.c $(FONT_SRCS) lv_conf.h $(DEPS)
	$(CC) $(LV_CFLAGS) \
	  -o $@ render_docs.c harness.c ../src/screens.c ../src/hero_size.c \
	  ../src/baseline.c $(FONT_SRCS) $(LV_SRCS)

quick: $(LOGIC_BINS)
	@fail=0; for t in $(LOGIC_BINS); do ./$$t || fail=1; echo; done; exit $$fail

test: $(BINS)
	@fail=0; for t in $(BINS); do ./$$t || fail=1; echo; done; exit $$fail

clean:
	rm -f $(BINS)
	rm -rf *.dSYM

test_tapdetect: test_tapdetect.c ../src/tapdetect.c ../include/tapdetect.h
	$(CC) $(CFLAGS) -o $@ test_tapdetect.c ../src/tapdetect.c

test_tapstatus: test_tapstatus.c ../src/tapstatus.c ../include/tapstatus.h
	$(CC) $(CFLAGS) -o $@ test_tapstatus.c ../src/tapstatus.c

test_provision: test_provision.c ../src/provision.c ../include/provision.h
	$(CC) $(CFLAGS) -o $@ test_provision.c ../src/provision.c

test_stripe_key: test_stripe_key.c ../src/stripe_key.c ../include/stripe_key.h
	$(CC) $(CFLAGS) -o $@ test_stripe_key.c ../src/stripe_key.c

test_mrr: test_mrr.c ../src/mrr.c ../include/mrr.h
	$(CC) $(CFLAGS) -o $@ test_mrr.c ../src/mrr.c

test_format: test_format.c ../src/format.c ../src/hero_size.c
	$(CC) $(CFLAGS) -o $@ test_format.c ../src/format.c ../src/hero_size.c


test_freshness: test_freshness.c ../src/freshness.c ../include/freshness.h
	$(CC) $(CFLAGS) -o $@ test_freshness.c ../src/freshness.c

test_events: test_events.c ../src/events.c ../include/events.h
	$(CC) $(CFLAGS) -o $@ test_events.c ../src/events.c

test_rotation: test_rotation.c ../src/rotation.c ../include/rotation.h
	$(CC) $(CFLAGS) -o $@ test_rotation.c ../src/rotation.c

test_cache: test_cache.c ../src/cache.c ../include/cache.h
	$(CC) $(CFLAGS) -o $@ test_cache.c ../src/cache.c

test_state: test_state.c ../src/state.c ../include/state.h
	$(CC) $(CFLAGS) -o $@ test_state.c ../src/state.c

test_battery: test_battery.c ../src/battery.c ../include/battery.h
	$(CC) $(CFLAGS) -o $@ test_battery.c ../src/battery.c

test_orientation: test_orientation.c ../src/orientation.c ../include/orientation.h
	$(CC) $(CFLAGS) -o $@ test_orientation.c ../src/orientation.c

test_sparkline: test_sparkline.c ../src/sparkline.c ../src/history.c ../include/sparkline.h
	$(CC) $(CFLAGS) -o $@ test_sparkline.c ../src/sparkline.c ../src/history.c

test_jsonstream: test_jsonstream.c ../src/jsonstream.c ../src/mrr.c ../include/jsonstream.h
	$(CC) $(CFLAGS) -o $@ test_jsonstream.c ../src/jsonstream.c ../src/mrr.c


test_history: test_history.c ../src/history.c ../include/history.h
	$(CC) $(CFLAGS) -o $@ test_history.c ../src/history.c

test_geometry: test_geometry.c ../src/geometry.c ../include/geometry.h
	$(CC) $(CFLAGS) -o $@ test_geometry.c ../src/geometry.c -lm

test_wifi_retry: test_wifi_retry.c ../src/wifi_retry.c ../include/wifi_retry.h
	$(CC) $(CFLAGS) -o $@ test_wifi_retry.c ../src/wifi_retry.c

test_layout_c6: test_layout_c6.c ../src/geometry.c ../src/hero_size.c ../include/layout_c6.h
	$(CC) $(CFLAGS) -o $@ test_layout_c6.c ../src/geometry.c ../src/hero_size.c -lm
