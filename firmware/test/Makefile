# Host-side tests. No ESP-IDF or hardware required.
#
#   make        build and run
#   make clean

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0

DEPS    := ../main/hero_size.h ../main/layout.h ../main/baseline.h

BIN        := test_hero_size
FONT_BIN   := test_font_coverage
LAYOUT_BIN := test_layout

BINS := $(BIN) $(FONT_BIN) $(LAYOUT_BIN)

.PHONY: all test clean

all: test

$(BIN): test_hero_size.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $(BIN) test_hero_size.c ../main/hero_size.c

$(FONT_BIN): test_font_coverage.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $(FONT_BIN) test_font_coverage.c ../main/hero_size.c

$(LAYOUT_BIN): test_layout.c ../main/hero_size.c ../main/baseline.c $(DEPS)
	$(CC) $(CFLAGS) -o $(LAYOUT_BIN) test_layout.c ../main/hero_size.c ../main/baseline.c

test: $(BINS)
	@fail=0; for t in $(BINS); do ./$$t || fail=1; echo; done; exit $$fail

clean:
	rm -f $(BINS)
	rm -rf *.dSYM
