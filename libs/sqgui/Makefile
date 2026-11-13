# libs/sqgui: immediate-mode widgets over a 32-bit pixel canvas (C11).
# The core has no OS calls and no allocation. Consumers add
# -I$(SQGUI)/include and link $(SQGUI)/build/libsqgui.a; for a window,
# also $(SQGUI)/build/sqgui_sdl.o and SDL2 (built when pkg-config finds it).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude
AR ?= ar
PYTHON ?= python3
BUILD = build

HAS_SDL := $(shell pkg-config --exists sdl2 && echo 1)

# Rendering of the test panel, pinned: change it only on purpose
GOLDEN = 0x201dc427

CORE = $(BUILD)/sqgui_draw.o $(BUILD)/sqgui_ui.o

.PHONY: all test font-check clean

all: $(BUILD)/libsqgui.a $(if $(HAS_SDL),$(BUILD)/sqgui_sdl.o $(BUILD)/sqgui_demo)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqgui_draw.o: src/sqgui_draw.c src/font5x7.inc include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqgui_draw.c -o $@

$(BUILD)/sqgui_ui.o: src/sqgui_ui.c include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqgui_ui.c -o $@

$(BUILD)/libsqgui.a: $(CORE)
	$(AR) rcs $@ $^

$(BUILD)/sqgui_sdl.o: src/sqgui_sdl.c include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl2) -c src/sqgui_sdl.c -o $@

$(BUILD)/sqgui_demo: tools/sqgui_demo.c $(BUILD)/libsqgui.a $(BUILD)/sqgui_sdl.o
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl2) $< $(BUILD)/sqgui_sdl.o $(BUILD)/libsqgui.a -o $@ $(shell pkg-config --libs sdl2)

$(BUILD)/test_sqgui: tests/test_sqgui.c $(BUILD)/libsqgui.a
	$(CC) $(CFLAGS) $(if $(filter-out 0x0,$(GOLDEN)),-DSQGUI_GOLDEN=$(GOLDEN)u) $< $(BUILD)/libsqgui.a -o $@

font-check:
	$(PYTHON) -I tools/mkfont.py --check

test: $(BUILD)/test_sqgui font-check
	$(BUILD)/test_sqgui $(BUILD)/panel.ppm
	@if [ -n "$(HAS_SDL)" ]; then \
		$(MAKE) $(BUILD)/sqgui_demo && \
		SDL_VIDEODRIVER=dummy $(BUILD)/sqgui_demo --frames 30; \
	else echo "sqgui: no SDL2, window back end not tested"; fi

clean:
	rm -rf $(BUILD)
