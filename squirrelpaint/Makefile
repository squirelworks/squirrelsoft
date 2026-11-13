# SquirrelPaint: a paint program (desktop) on libs/sqgui.
CC ?= gcc
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude -I$(SQGUI)/include
BUILD = build

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/paint_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
$(BUILD)/paint_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)
else
GUI_RUN = $(BUILD)/paint_nosdl.o
endif

# The core: no allocation, no stdio (it goes to Squirrel OS/2 as it is)
CORE = $(BUILD)/paint.o $(BUILD)/paintsel.o $(BUILD)/painttools.o $(BUILD)/paintio.o $(BUILD)/paintz.o
OBJ = $(CORE) $(BUILD)/paintgui.o
HDR = $(wildcard include/*.h) src/paint_int.h $(SQGUI)/include/sqgui.h $(SQGUI)/src/font5x7.inc
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all test freestanding clean $(SQGUI_LIB)

all: $(BUILD)/squirrelpaint

$(BUILD):
	mkdir -p $(BUILD)

$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelpaint: src/main.c $(OBJ) $(GUI_RUN) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(GUI_RUN) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_paint: tests/test_paint.c $(OBJ) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQGUI_LIB) -o $@

# The core builds with -ffreestanding and needs nothing from outside it but
# memcpy, memmove, memset and strlen
freestanding: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/paint.c -o $(BUILD)/fs_paint.o
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/paintio.c -o $(BUILD)/fs_paintio.o
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/paintsel.c -o $(BUILD)/fs_paintsel.o
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/paintz.c -o $(BUILD)/fs_paintz.o
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/painttools.c -o $(BUILD)/fs_painttools.o
	$(LD) -r $(BUILD)/fs_paint.o $(BUILD)/fs_paintsel.o $(BUILD)/fs_painttools.o $(BUILD)/fs_paintio.o $(BUILD)/fs_paintz.o -o $(BUILD)/fs_core.o
	@bad=$$(nm -u $(BUILD)/fs_core.o | awk '$$1=="U"{print $$2}' | grep -v -x -E 'memcpy|memmove|memset|strlen' | sort -u); \
	if [ -n "$$bad" ]; then echo "the core uses: $$bad"; exit 1; fi; echo "squirrelpaint core freestanding ok"

test: $(BUILD)/test_paint $(BUILD)/squirrelpaint freestanding
	$(BUILD)/test_paint $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelpaint --gui-frames 10 && echo "squirrelpaint window ok"; \
	else echo "squirrelpaint: no SDL2, window not tested"; fi
	@echo "squirrelpaint tests ok"

clean:
	rm -rf $(BUILD)
