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

LOGIC_BINS  := test_hero_size test_font_coverage test_layout test_tapdetect
SCREEN_BIN  := test_screens
BINS        := $(LOGIC_BINS) $(SCREEN_BIN)

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
