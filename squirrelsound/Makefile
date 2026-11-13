CC ?= gcc
SQDSP = ../libs/sqdsp
SQGUI = ../libs/sqgui
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude -I$(SQDSP)/include -I$(SQGUI)/include
LDFLAGS ?= -lm
BUILD = build

# Optional live backends
HAS_SDL := $(shell pkg-config --exists sdl2 && echo 1)
HAS_ALSA := $(shell pkg-config --exists alsa && echo 1)

ifeq ($(HAS_SDL),1)
CFLAGS += -DSQSOUND_HAS_SDL $(shell pkg-config --cflags sdl2)
LDFLAGS += $(shell pkg-config --libs sdl2)
SQGUI_SDL = $(SQGUI)/build/sqgui_sdl.o
endif
ifeq ($(HAS_ALSA),1)
CFLAGS += -DSQSOUND_HAS_ALSA $(shell pkg-config --cflags alsa)
LDFLAGS += $(shell pkg-config --libs alsa)
endif

LIB_OBJ = $(BUILD)/wavio.o $(BUILD)/synth.o $(BUILD)/spectrogram.o \
	$(BUILD)/live_audio.o $(BUILD)/live_engine.o $(BUILD)/live_prefs.o \
	$(BUILD)/live_view_headless.o $(BUILD)/live_view_sdl.o $(BUILD)/live_gui.o $(BUILD)/live_record.o

.PHONY: all test clean android android-test

all: $(BUILD)/squirrelsound $(BUILD)/test_sqdsp $(BUILD)/test_live $(BUILD)/test_live_gui

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

$(BUILD):
	mkdir -p $(BUILD)

# Shared DSP core (libs/sqdsp); always delegate so its own deps are honoured.
SQDSP_LIB = $(SQDSP)/build/libsqdsp.a
.PHONY: $(SQDSP_LIB)
$(SQDSP_LIB):
	$(MAKE) -C $(SQDSP)

# Shared GUI toolkit (libs/sqgui); the SDL back end object only when SDL2 is here.
SQGUI_LIB = $(SQGUI)/build/libsqgui.a
.PHONY: $(SQGUI_LIB)
$(SQGUI_LIB):
	$(MAKE) -C $(SQGUI) build/libsqgui.a $(if $(SQGUI_SDL),build/sqgui_sdl.o)
ifneq ($(SQGUI_SDL),)
$(SQGUI_SDL): $(SQGUI_LIB)
	@:
endif
DEPS = $(SQDSP_LIB) $(SQGUI_LIB) $(SQGUI_SDL)
LINK = $(LIB_OBJ) $(SQDSP_LIB) $(SQGUI_SDL) $(SQGUI_LIB)

$(BUILD)/wavio.o: src/wavio.c include/wavio.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/wavio.c -o $@

$(BUILD)/synth.o: src/synth.c include/synth.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/synth.c -o $@

$(BUILD)/spectrogram.o: src/spectrogram.c include/spectrogram.h $(SQDSP)/include/sqdsp.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/spectrogram.c -o $@

$(BUILD)/live_audio.o: src/live_audio.c include/live_audio.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_audio.c -o $@

$(BUILD)/live_engine.o: src/live_engine.c include/live_engine.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_engine.c -o $@

$(BUILD)/live_prefs.o: src/live_prefs.c include/live_prefs.h include/live_engine.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_prefs.c -o $@

$(BUILD)/live_record.o: src/live_record.c include/live_record.h include/live_audio.h include/wavio.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_record.c -o $@

$(BUILD)/live_gui.o: src/live_gui.c include/live_gui.h include/live_engine.h $(SQGUI)/include/sqgui.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_gui.c -o $@

$(BUILD)/live_view_headless.o: src/live_view_headless.c include/live_view.h include/live_gui.h include/live_record.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_view_headless.c -o $@

$(BUILD)/live_view_sdl.o: src/live_view_sdl.c include/live_view.h include/live_gui.h include/live_prefs.h include/live_record.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_view_sdl.c -o $@

$(BUILD)/squirrelsound: src/main.c $(LIB_OBJ) $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(LINK) -o $@ $(LDFLAGS)

$(BUILD)/test_sqdsp: tests/test_sqdsp.c $(LIB_OBJ) $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqdsp.c $(LINK) -o $@ $(LDFLAGS)

$(BUILD)/test_live: tests/test_live.c $(LIB_OBJ) $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_live.c $(LINK) -o $@ $(LDFLAGS)

$(BUILD)/test_live_gui: tests/test_live_gui.c $(LIB_OBJ) $(DEPS) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_live_gui.c $(LINK) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqdsp $(BUILD)/test_live $(BUILD)/test_live_gui $(BUILD)/squirrelsound
	$(BUILD)/test_sqdsp
	$(BUILD)/test_live
	$(BUILD)/test_live_gui $(BUILD)/live_gui.ppm
	sh tests/test_capture.sh $(BUILD)
	@tmp=/tmp/sqsound_a440.wav; svg=/tmp/sqsound_a440.svg; \
	$(BUILD)/squirrelsound synth $$tmp --freq 440 --seconds 1.2 --harmonics 3 --rate 44100 && \
	$(BUILD)/squirrelsound pitch $$tmp | tee /tmp/sqsound_pitch.txt | grep -E '44[0-9]\.' && \
	$(BUILD)/squirrelsound spectrum $$tmp --fft 4096 | tee /tmp/sqsound_spec.txt | grep -q dominant && \
	$(BUILD)/squirrelsound thd $$tmp --fund 440 | tee /tmp/sqsound_thd.txt | grep -q THD && \
	$(BUILD)/squirrelsound spectrogram $$tmp $$svg --fft 1024 && \
	$(BUILD)/squirrelsound live --source demo --headless --frames 20 --fft 2048 --freq 440 && \
	$(BUILD)/squirrelsound live --source demo --gui-snapshot $(BUILD)/live_snap.ppm --frames 20 --freq 440 && \
	test -s $(BUILD)/live_snap.ppm && \
	for v in octave third scope; do \
		$(BUILD)/squirrelsound live --source demo --gui-snapshot $(BUILD)/live_$$v.ppm --view $$v --cal 3 --frames 10 >/dev/null || exit 1; \
	done && \
	! $(BUILD)/squirrelsound live --source demo --headless --view sideways 2>/dev/null && \
	$(BUILD)/squirrelsound live --source demo --headless --frames 10 --freq 1000 --harmonics 1 --record $(BUILD)/live_rec.wav | grep -q '^recorded' && \
	$(BUILD)/squirrelsound pitch $(BUILD)/live_rec.wav | grep -qE 'autocorr\) ~= (99[0-9]|10[0-2][0-9])\.' && \
	test -s $$svg && echo "squirrelsound cli smoke ok"

clean:
	rm -rf $(BUILD)
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
