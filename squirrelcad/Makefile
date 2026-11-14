CC ?= gcc
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
CFLAGS += -Iinclude -I$(SQGUI)/include
LDFLAGS ?= -lm
BUILD = build
OBJ = $(BUILD)/sqcad.o $(BUILD)/sqc_dxf.o $(BUILD)/sqc_cmd.o $(BUILD)/sqc_plot.o

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/sqc_gui_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
SDL_LIBS = $(shell pkg-config --libs sdl2)
$(BUILD)/sqc_gui_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
else
GUI_RUN = $(BUILD)/sqc_gui_nosdl.o
endif
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
GUI_OBJ = $(BUILD)/sqc_gui.o

.PHONY: all test clean $(SQGUI_LIB)

all: $(BUILD)/squirrelcad $(BUILD)/test_sqcad $(BUILD)/test_dxf $(BUILD)/test_cmd $(BUILD)/test_gui

$(BUILD):
	mkdir -p $(BUILD)

$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif

$(BUILD)/sqc_gui.o: src/sqc_gui.c include/sqc_gui.h include/sqc_cmd.h include/sqcad.h $(SQGUI)/include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_gui.c -o $@

$(BUILD)/sqc_gui_sdl.o: src/sqc_gui_sdl.c include/sqc_gui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_gui_sdl.c -o $@

$(BUILD)/sqc_gui_nosdl.o: src/sqc_gui_nosdl.c include/sqc_gui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_gui_nosdl.c -o $@

$(BUILD)/sqcad.o: src/sqcad.c include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqcad.c -o $@

$(BUILD)/sqc_dxf.o: src/sqc_dxf.c include/sqc_dxf.h include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_dxf.c -o $@

$(BUILD)/sqc_cmd.o: src/sqc_cmd.c include/sqc_cmd.h include/sqcad.h include/sqc_dxf.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_cmd.c -o $@

$(BUILD)/sqc_plot.o: src/sqc_plot.c include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_plot.c -o $@

$(BUILD)/squirrelcad: src/main.c $(OBJ) $(GUI_OBJ) $(GUI_RUN) $(SQGUI_SDL) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) $(GUI_OBJ) $(GUI_RUN) $(SQGUI_SDL) $(SQGUI_LIB) -o $@ $(LDFLAGS) $(SDL_LIBS)

$(BUILD)/test_gui: tests/test_gui.c $(OBJ) $(GUI_OBJ) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_gui.c $(OBJ) $(GUI_OBJ) $(SQGUI_LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_sqcad: tests/test_sqcad.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqcad.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_dxf: tests/test_dxf.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_dxf.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_cmd: tests/test_cmd.c include/sqc_cmd.h $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_cmd.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqcad $(BUILD)/test_dxf $(BUILD)/test_cmd $(BUILD)/test_gui $(BUILD)/squirrelcad
	$(BUILD)/test_sqcad
	$(BUILD)/test_cmd
	$(BUILD)/test_gui $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	cd $(CURDIR) && $(BUILD)/test_dxf
	@tmp=$(BUILD)/sqc_cli_smoke.sqdwg; from=$(BUILD)/sqc_from_dxf.sqdwg; out=$(BUILD)/sqc_out.dxf; \
	$(BUILD)/squirrelcad new $$tmp && \
	$(BUILD)/squirrelcad LINE $$tmp 0,0 3,4 && \
	$(BUILD)/squirrelcad LEN $$tmp 2 | grep -q 5 && \
	$(BUILD)/squirrelcad IMPORT-DXF $$from samples/autocad_bridge.dxf && \
	$(BUILD)/squirrelcad LIST $$from | grep -q ents= && \
	$(BUILD)/squirrelcad EXPORT-DXF $$from $$out && \
	$(BUILD)/squirrelcad PROBE samples/autocad_bridge.dxf | grep -q dxf && \
	rm -f $$tmp $$from $$out && echo "squirrelcad cli+dxf smoke ok"
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelcad gui samples/autocad_bridge.dxf --frames 10 && echo "squirrelcad window ok"; \
	else echo "squirrelcad: no SDL2, window not tested"; fi

clean:
	rm -rf $(BUILD)
