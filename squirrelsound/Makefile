CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build

LIB_SRC = src/sqnr.c src/wavio.c src/synth.c src/spectrogram.c
LIB_OBJ = $(BUILD)/sqnr.o $(BUILD)/wavio.o $(BUILD)/synth.o $(BUILD)/spectrogram.o

.PHONY: all test clean

all: $(BUILD)/squirrelsound $(BUILD)/test_sqnr

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

$(BUILD)/squirrelsound: src/main.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(LIB_OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqnr: tests/test_sqnr.c $(LIB_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqnr.c $(LIB_OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqnr $(BUILD)/squirrelsound
	$(BUILD)/test_sqnr
	@tmp=/tmp/sqsound_a440.wav; svg=/tmp/sqsound_a440.svg; \
	$(BUILD)/squirrelsound synth $$tmp --freq 440 --seconds 1.2 --harmonics 3 --rate 44100 && \
	$(BUILD)/squirrelsound pitch $$tmp | tee /tmp/sqsound_pitch.txt | grep -E '44[0-9]\.' && \
	$(BUILD)/squirrelsound spectrum $$tmp --fft 4096 | tee /tmp/sqsound_spec.txt | grep -q dominant && \
	$(BUILD)/squirrelsound thd $$tmp --fund 440 | tee /tmp/sqsound_thd.txt | grep -q THD && \
	$(BUILD)/squirrelsound spectrogram $$tmp $$svg --fft 1024 && \
	test -s $$svg && echo "squirrelsound cli smoke ok"

clean:
	rm -rf $(BUILD)
