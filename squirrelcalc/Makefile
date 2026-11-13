# SquirrelCalc: graphing calculator (desktop) on libs/sqmath and libs/sqgui.
CC ?= gcc
SQMATH = ../libs/sqmath
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude -I$(SQMATH)/include -I$(SQGUI)/include
LDLIBS = -lm
BUILD = build

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/calc_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
$(BUILD)/calc_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)
else
GUI_RUN = $(BUILD)/calc_nosdl.o
endif

OBJ = $(BUILD)/calcgraph.o $(BUILD)/calcgui.o $(BUILD)/calcsheet.o $(BUILD)/calcstats.o
HDR = $(wildcard include/*.h) $(SQMATH)/include/sqmath.h $(SQGUI)/include/sqgui.h
SQMATH_LIB = $(SQMATH)/build/libsqmath.a
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQMATH_LIB) $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all test clean $(SQMATH_LIB) $(SQGUI_LIB)

all: $(BUILD)/squirrelcalc

$(BUILD):
	mkdir -p $(BUILD)

$(SQMATH_LIB):
	$(MAKE) -C $(SQMATH) build/libsqmath.a

$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelcalc: src/main.c $(OBJ) $(GUI_RUN) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(GUI_RUN) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_calc: tests/test_calc.c $(OBJ) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQMATH_LIB) $(SQGUI_LIB) -o $@ -lm

$(BUILD)/test_sheet: tests/test_sheet.c $(BUILD)/calcsheet.o $(BUILD)/calcstats.o $(SQMATH_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(BUILD)/calcsheet.o $(BUILD)/calcstats.o $(SQMATH_LIB) -o $@ -lm

test: $(BUILD)/test_calc $(BUILD)/test_sheet $(BUILD)/squirrelcalc
	$(BUILD)/test_sheet
	$(BUILD)/test_calc $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelcalc --fn "sin(x)" --roots --gui-frames 10 && echo "squirrelcalc window ok"; \
	else echo "squirrelcalc: no SDL2, window not tested"; fi
	@echo "squirrelcalc tests ok"

clean:
	rm -rf $(BUILD)
