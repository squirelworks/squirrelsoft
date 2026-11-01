CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build

# Optional live backends
HAS_SDL := $(shell pkg-config --exists sdl2 && echo 1)
HAS_ALSA := $(shell pkg-config --exists alsa && echo 1)

ifeq ($(HAS_SDL),1)
CFLAGS += -DSQSOUND_HAS_SDL $(shell pkg-config --cflags sdl2)
LDFLAGS += $(shell pkg-config --libs sdl2)
endif
ifeq ($(HAS_ALSA),1)
CFLAGS += -DSQSOUND_HAS_ALSA $(shell pkg-config --cflags alsa)
LDFLAGS += $(shell pkg-config --libs alsa)
endif

LIB_OBJ = $(BUILD)/sqnr.o $(BUILD)/wavio.o $(BUILD)/synth.o $(BUILD)/spectrogram.o \
	$(BUILD)/live_audio.o $(BUILD)/live_engine.o $(BUILD)/live_view_headless.o \
	$(BUILD)/live_view_sdl.o

.PHONY: all test clean

all: $(BUILD)/squirrelsound $(BUILD)/test_sqnr $(BUILD)/test_live

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqnr.o: src/sqnr.c include/sqnr.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqnr.c -o $@

$(BUILD)/wavio.o: src/wavio.c include/wavio.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/wavio.c -o $@

$(BUILD)/synth.o: src/synth.c include/synth.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/synth.c -o $@

$(BUILD)/spectrogram.o: src/spectrogram.c include/spectrogram.h include/sqnr.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/spectrogram.c -o $@

$(BUILD)/live_audio.o: src/live_audio.c include/live_audio.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_audio.c -o $@

$(BUILD)/live_engine.o: src/live_engine.c include/live_engine.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_engine.c -o $@

$(BUILD)/live_view_headless.o: src/live_view_headless.c include/live_view.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_view_headless.c -o $@

$(BUILD)/live_view_sdl.o: src/live_view_sdl.c include/live_view.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/live_view_sdl.c -o $@

$(BUILD)/squirrelsound: src/main.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(LIB_OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqnr: tests/test_sqnr.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqnr.c $(LIB_OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_live: tests/test_live.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_live.c $(LIB_OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqnr $(BUILD)/test_live $(BUILD)/squirrelsound
	$(BUILD)/test_sqnr
	$(BUILD)/test_live
	@tmp=/tmp/sqsound_a440.wav; svg=/tmp/sqsound_a440.svg; \
	$(BUILD)/squirrelsound synth $$tmp --freq 440 --seconds 1.2 --harmonics 3 --rate 44100 && \
	$(BUILD)/squirrelsound pitch $$tmp | tee /tmp/sqsound_pitch.txt | grep -E '44[0-9]\.' && \
	$(BUILD)/squirrelsound spectrum $$tmp --fft 4096 | tee /tmp/sqsound_spec.txt | grep -q dominant && \
	$(BUILD)/squirrelsound thd $$tmp --fund 440 | tee /tmp/sqsound_thd.txt | grep -q THD && \
	$(BUILD)/squirrelsound spectrogram $$tmp $$svg --fft 1024 && \
	$(BUILD)/squirrelsound live --source demo --headless --frames 20 --fft 2048 --freq 440 && \
	test -s $$svg && echo "squirrelsound cli smoke ok"

clean:
	rm -rf $(BUILD)
