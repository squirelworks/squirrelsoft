# SquirrelGames: patience card games (desktop) on libs/sqgui.
CC ?= gcc
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude -I$(SQGUI)/include
BUILD = build

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/games_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
$(BUILD)/games_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)
else
GUI_RUN = $(BUILD)/games_nosdl.o
endif

# The core: no allocation, no stdio (it goes to Squirrel OS/2 as it is)
CORE = $(BUILD)/cards.o $(BUILD)/mines.o
OBJ = $(CORE) $(BUILD)/gamesgui.o
HDR = $(wildcard include/*.h) $(SQGUI)/include/sqgui.h
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all test freestanding clean $(SQGUI_LIB)

all: $(BUILD)/squirrelgames

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

$(BUILD)/squirrelgames: src/main.c $(OBJ) $(GUI_RUN) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(GUI_RUN) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_games: tests/test_games.c $(OBJ) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQGUI_LIB) -o $@

# The core builds with -ffreestanding and needs nothing from outside it but
# memcpy, memmove, memset and strlen
freestanding: | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/cards.c -o $(BUILD)/fs_cards.o
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 -ffreestanding -Iinclude -c src/mines.c -o $(BUILD)/fs_mines.o
	@bad=$$(nm -u $(BUILD)/fs_cards.o $(BUILD)/fs_mines.o | awk '$$1=="U"{print $$2}' | grep -v -x -E 'memcpy|memmove|memset|strlen' | sort -u); \
	if [ -n "$$bad" ]; then echo "the core uses: $$bad"; exit 1; fi; echo "squirrelgames core freestanding ok"

test: $(BUILD)/test_games $(BUILD)/squirrelgames freestanding
	$(BUILD)/test_games $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelgames --gui-frames 10 && echo "squirrelgames window ok"; \
	else echo "squirrelgames: no SDL2, window not tested"; fi
	@echo "squirrelgames tests ok"

clean:
	rm -rf $(BUILD)
