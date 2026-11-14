# SquirrelDebug: a debugger for Squirrel OS/2 over QEMU's gdbstub or a serial line, on libs/sqgui.
CC ?= gcc
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude -I$(SQGUI)/include
BUILD = build

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/dbg_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
$(BUILD)/dbg_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)
else
GUI_RUN = $(BUILD)/dbg_nosdl.o
endif

# The core: no allocation, no stdio (it goes to Squirrel OS/2 as it is)
CORE_NAMES = rsp x86dis dbg stub
CORE = $(CORE_NAMES:%=$(BUILD)/%.o)
OBJ = $(CORE) $(BUILD)/dbggui.o $(BUILD)/link.o
HDR = $(wildcard include/*.h) $(SQGUI)/include/sqgui.h
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all test freestanding clean $(SQGUI_LIB)

all: $(BUILD)/squirreldebug

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

$(BUILD)/squirreldebug: src/main.c $(OBJ) $(GUI_RUN) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(GUI_RUN) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_debug: tests/test_debug.c $(OBJ) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) -Itests $< $(OBJ) $(SQGUI_LIB) -o $@

# The core builds with -ffreestanding and needs nothing from outside it but
# memcpy, memmove, memset and strlen
freestanding: | $(BUILD)
	@if [ "$$(uname -s)" != Linux ]; then \
		echo "squirreldebug: freestanding ELF check is Linux-only; skipped on $$(uname -s)"; \
	else \
		for f in $(CORE_NAMES); do \
			$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -U_FORTIFY_SOURCE -Iinclude -c src/$$f.c -o $(BUILD)/fs_$$f.o || exit 1; \
		done && \
		ld -r -o $(BUILD)/fs_core.o $(CORE_NAMES:%=$(BUILD)/fs_%.o) && \
		bad=$$(nm -u $(BUILD)/fs_core.o | awk '$$1=="U"{print $$2}' | grep -v -x -E 'memcpy|memmove|memset|strlen' | sort -u); \
		if [ -n "$$bad" ]; then echo "the core uses: $$bad"; exit 1; fi; \
		echo "squirreldebug core freestanding ok"; \
	fi

test: $(BUILD)/test_debug $(BUILD)/squirreldebug freestanding
	$(BUILD)/test_debug $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	sh tests/qemu_smoke.sh $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirreldebug --gui-frames 10 && echo "squirreldebug window ok"; \
	else echo "squirreldebug: no SDL2, window not tested"; fi
	@echo "squirreldebug tests ok"

clean:
	rm -rf $(BUILD)
