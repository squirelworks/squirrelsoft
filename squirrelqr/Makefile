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

# Webcam: V4L2 on Linux when videodev2.h is present; stub elsewhere (mf/avf until PKG).
WEBCAM ?= $(shell printf '#include <linux/videodev2.h>\nint main(void){return 0;}\n' | $(CC) -x c - -o /dev/null 2>/dev/null && echo v4l2 || echo stub)
ifeq ($(WEBCAM),v4l2)
WEBCAM_OBJ = $(BUILD)/webcam_v4l2.o
else
WEBCAM_OBJ = $(BUILD)/webcam_stub.o
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

OBJ = $(BUILD)/qrimage.o $(BUILD)/qrpng.o $(BUILD)/qrtext.o $(BUILD)/qrdecode.o $(BUILD)/qrmake.o \
	$(BUILD)/qrgui.o $(BUILD)/capture_$(CAPTURE).o $(WEBCAM_OBJ)
HDR = $(wildcard desktop/include/*.h) $(SQQR)/include/sqqr.h $(SQGUI)/include/sqgui.h
SQQR_LIB = $(SQQR)/build/libsqqr.a
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
LIBS = $(SQQR_LIB) $(SQGUI_SDL) $(SQGUI_LIB)

.PHONY: all desktop test test-desktop test-jni test-x11 test-webcam serve clean android android-test $(SQQR_LIB) $(SQGUI_LIB)

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

$(BUILD)/test_webcam: desktop/tests/test_webcam.c $(OBJ) $(LIBS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(LIBS) -o $@ $(LDLIBS)

test: test-desktop test-jni
	python3 tests/test_web_static.py
	@echo "squirrelqr tests ok"

test-desktop: $(BUILD)/test_desktop $(BUILD)/test_gui $(BUILD)/test_webcam $(BUILD)/squirrelqr
	$(BUILD)/test_desktop $(SQQR)/fixtures/symbols.txt $(BUILD)
	$(BUILD)/test_gui $(BUILD)
	$(BUILD)/test_webcam $(BUILD)
	@if [ "$(HAS_SDL)" = 1 ]; then \
		SDL_VIDEODRIVER=dummy $(BUILD)/squirrelqr --gui-frames 10 $(BUILD)/fmt_P5.img && echo "squirrelqr window ok"; \
	else echo "squirrelqr: no SDL2, window not tested"; fi
	sh desktop/tests/cli_smoke.sh $(BUILD)
	sh desktop/tests/webcam_smoke.sh $(BUILD)
	python3 -I desktop/tests/test_png.py $(BUILD)
	@if [ "$(CAPTURE)" = x11 ] && command -v xvfb-run >/dev/null; then \
		xvfb-run -a -s "-screen 0 800x600x24" $(BUILD)/test_desktop --x11 $(SQQR)/fixtures/symbols.txt $(BUILD); \
	else echo "skipping the X11 capture test (no X11 build or no xvfb-run)"; fi

test-webcam: $(BUILD)/test_webcam $(BUILD)/squirrelqr
	$(BUILD)/test_webcam $(BUILD)
	sh desktop/tests/webcam_smoke.sh $(BUILD)

# The Android JNI bridge built for this machine and driven from a JVM
# (android/jni-smoke): skipped when there is no JDK.
JDK ?= $(shell if [ -n "$$JAVA_HOME" ]; then echo "$$JAVA_HOME"; \
	elif command -v javac >/dev/null; then dirname "$$(dirname "$$(readlink -f "$$(command -v javac)")")"; fi)
JNI_SRC = android/app/src/main/cpp/sqqr_jni.c $(SQQR)/src/sqqr.c $(SQQR)/src/sqqr_locate.c \
	$(SQQR)/src/sqqr_encode.c $(SQQR)/src/sqqr_content.c

test-jni: | $(BUILD)
	@if [ -n "$(JDK)" ] && [ -f "$(JDK)/include/jni.h" ] && [ -x "$(JDK)/bin/javac" ]; then \
		set -e; plat=$$(ls -d "$(JDK)"/include/*/ | head -1); \
		$(CC) -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC -I"$(JDK)/include" -I"$$plat" \
			-I$(SQQR)/include $(JNI_SRC) -lm -o $(BUILD)/libsqqr_jni.so; \
		mkdir -p $(BUILD)/jni-smoke; \
		"$(JDK)/bin/javac" -d $(BUILD)/jni-smoke android/jni-smoke/works/squirrel/qr/SqQr.java; \
		"$(JDK)/bin/java" -cp $(BUILD)/jni-smoke works.squirrel.qr.SqQr "$$(pwd)/$(BUILD)/libsqqr_jni.so"; \
	else echo "squirrelqr: no JDK, JNI smoke test skipped"; fi

serve:
	python3 -m http.server 8765 --directory web-demo

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	rm -rf $(BUILD)
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
