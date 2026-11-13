CC ?= gcc
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
CFLAGS += -Iinclude -I$(SQGUI)/include
LDFLAGS ?= -lm

BUILD = build
OBJ = $(BUILD)/sqstation.o $(BUILD)/sqs_dxf.o

# The window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/sqs_gui_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
SDL_LIBS = $(shell pkg-config --libs sdl2)
$(BUILD)/sqs_gui_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
else
GUI_RUN = $(BUILD)/sqs_gui_nosdl.o
endif
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
GUI_OBJ = $(BUILD)/sqs_gui.o

.PHONY: all test clean $(SQGUI_LIB)

all: $(BUILD)/squirrelstation $(BUILD)/test_sqstation $(BUILD)/test_dxf $(BUILD)/test_elements $(BUILD)/test_gui

$(BUILD):
	mkdir -p $(BUILD)

$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif

$(BUILD)/sqs_gui.o: src/sqs_gui.c include/sqs_gui.h include/sqstation.h $(SQGUI)/include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqs_gui.c -o $@

$(BUILD)/sqs_gui_sdl.o: src/sqs_gui_sdl.c include/sqs_gui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqs_gui_sdl.c -o $@

$(BUILD)/sqs_gui_nosdl.o: src/sqs_gui_nosdl.c include/sqs_gui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqs_gui_nosdl.c -o $@

$(BUILD)/sqstation.o: src/sqstation.c include/sqstation.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqstation.c -o $@

$(BUILD)/sqs_dxf.o: src/sqs_dxf.c include/sqs_dxf.h include/sqstation.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqs_dxf.c -o $@

$(BUILD)/squirrelstation: src/main.c $(OBJ) $(GUI_OBJ) $(GUI_RUN) $(SQGUI_SDL) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) $(GUI_OBJ) $(GUI_RUN) $(SQGUI_SDL) $(SQGUI_LIB) -o $@ $(LDFLAGS) $(SDL_LIBS)

$(BUILD)/test_gui: tests/test_gui.c $(OBJ) $(GUI_OBJ) $(SQGUI_LIB) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_gui.c $(OBJ) $(GUI_OBJ) $(SQGUI_LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_sqstation: tests/test_sqstation.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqstation.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_dxf: tests/test_dxf.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_dxf.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_elements: tests/test_elements.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_elements.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqstation $(BUILD)/test_dxf $(BUILD)/test_elements $(BUILD)/test_gui $(BUILD)/squirrelstation
	$(BUILD)/test_sqstation
	$(BUILD)/test_elements
	$(BUILD)/test_gui $(BUILD)
	sh tests/cli_smoke.sh $(BUILD)
	cd $(CURDIR) && $(BUILD)/test_dxf
	@tmp=$$(mktemp /tmp/sqsXXXXXX.sqdgn); dxf=samples/microstation_bridge.dxf; \
	$(BUILD)/squirrelstation import-dxf $$tmp $$dxf && \
	$(BUILD)/squirrelstation list $$tmp | grep -q LINE && \
	$(BUILD)/squirrelstation export-dxf $$tmp /tmp/sqs_cli.dxf && \
	$(BUILD)/squirrelstation probe $$dxf | grep -q dxf && \
	rm -f $$tmp && echo "squirrelstation cli+dxf smoke ok"
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelstation gui samples/microstation_bridge.dxf --view iso --frames 10 && echo "squirrelstation window ok"; \
	else echo "squirrelstation: no SDL2, window not tested"; fi

clean:
	rm -rf $(BUILD)
