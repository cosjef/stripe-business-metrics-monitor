# Host-side tests. No ESP-IDF or hardware required.
#
#   make        build and run
#   make clean

CC      ?= cc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0

DEPS    := ../main/hero_size.h ../main/layout.h

BIN      := test_hero_size
FONT_BIN := test_font_coverage

.PHONY: all test clean

all: test

$(BIN): test_hero_size.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $(BIN) test_hero_size.c ../main/hero_size.c

$(FONT_BIN): test_font_coverage.c ../main/hero_size.c $(DEPS)
	$(CC) $(CFLAGS) -o $(FONT_BIN) test_font_coverage.c ../main/hero_size.c

test: $(BIN) $(FONT_BIN)
	./$(BIN)
	@echo
	./$(FONT_BIN)

clean:
	rm -f $(BIN) $(FONT_BIN)
