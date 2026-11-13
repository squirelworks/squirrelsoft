# SquirrelQR: Android app (android/) and desktop reader (desktop/), both on libs/sqqr.
CC ?= gcc
SQQR = ../libs/sqqr
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -D_POSIX_C_SOURCE=200809L -Idesktop/include -I$(SQQR)/include -I$(SQGUI)/include
LDLIBS = -lm
BUILD = build
QR_VERSION := $(shell sed -n 's/^ *versionName *= *"\([^"]*\)".*/\1/p' android/app/build.gradle.kts | head -1)
CFLAGS += -DQR_VERSION='"$(QR_VERSION)"'

# Screen capture back end: X11 when its headers are there (CAPTURE=none to skip).
CAPTURE ?= $(shell pkg-config --exists x11 && echo x11 || echo none)
ifeq ($(CAPTURE),x11)
CFLAGS += -DQR_TEST_X11 $(shell pkg-config --cflags x11)
LDLIBS += $(shell pkg-config --libs x11)
endif

# Shift JIS text needs iconv (in glibc; -liconv on macOS and MinGW).
ICONV_PROBE = printf '\043include <iconv.h>\nint main(void){return iconv_open("UTF-8","SHIFT_JIS")==(iconv_t)-1;}\n' | $(CC) -x c - -o /dev/null
ICONV ?= $(shell $(ICONV_PROBE) 2>/dev/null && echo libc || ($(ICONV_PROBE) -liconv 2>/dev/null && echo lib || echo none))
ifeq ($(ICONV),libc)
CFLAGS += -DQR_HAS_ICONV
else ifeq ($(ICONV),lib)
CFLAGS += -DQR_HAS_ICONV
LDLIBS += -liconv
endif

# The --gui window: libs/sqgui with its SDL2 back end when SDL2 is here.
HAS_SDL ?= $(shell pkg-config --exists sdl2 && echo 1)
ifeq ($(HAS_SDL),1)
GUI_RUN = $(BUILD)/qrgui_sdl.o
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
$(BUILD)/qrgui_sdl.o: CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2)
else
GUI_RUN = $(BUILD)/qrgui_nosdl.o
endif

OBJ = $(BUILD)/qrimage.o $(BUILD)/qrpng.o $(BUILD)/qrtext.o $(BUILD)/qrdecode.o \
	$(BUILD)/qrgui.o $(BUILD)/capture_$(CAPTURE).o
HDR = $(wildcard desktop/include/*.h) $(SQQR)/include/sqqr.h $(SQGUI)/include/sqgui.h
SQQR_LIB = $(SQQR)/build/libsqqr.a
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQQR_LIB) $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all desktop test test-desktop test-x11 serve clean android android-test $(SQQR_LIB) $(SQGUI_LIB)

all: desktop

desktop: $(BUILD)/squirrelqr

$(BUILD):
	mkdir -p $(BUILD)

$(SQQR_LIB):
	$(MAKE) -C $(SQQR) build/libsqqr.a

$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif

$(BUILD)/%.o: desktop/src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelqr: desktop/src/main.c $(OBJ) $(GUI_RUN) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(GUI_RUN) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_desktop: desktop/tests/test_desktop.c $(OBJ) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(LIBS) -o $@ $(LDLIBS)

$(BUILD)/test_gui: desktop/tests/test_gui.c $(OBJ) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(LIBS) -o $@ $(LDLIBS)

test: test-desktop
	python3 tests/test_web_static.py
	@echo "squirrelqr tests ok"

test-desktop: $(BUILD)/test_desktop $(BUILD)/test_gui $(BUILD)/squirrelqr
	$(BUILD)/test_desktop $(SQQR)/fixtures/symbols.txt $(BUILD)
	$(BUILD)/test_gui $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelqr --gui-frames 10 $(BUILD)/fmt_P5.img && echo "squirrelqr window ok"; \
	else echo "squirrelqr: no SDL2, window not tested"; fi
	sh desktop/tests/cli_smoke.sh $(BUILD)
	python3 -I desktop/tests/test_png.py $(BUILD)
	@if [ "$(CAPTURE)" = x11 ] && command -v xvfb-run >/dev/null; then \
		xvfb-run -a -s "-screen 0 800x600x24" $(BUILD)/test_desktop --x11 $(SQQR)/fixtures/symbols.txt $(BUILD); \
	else echo "skipping the X11 capture test (no X11 build or no xvfb-run)"; fi

serve:
	python3 -m http.server 8765 --directory web-demo

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	rm -rf $(BUILD)
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
