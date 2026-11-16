# SquirrelBusMon: Soft avionics data-bus analyzer for ISM fighter bus traffic.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/bm_util.o $(BUILD)/bm_catalog.o $(BUILD)/bm_frame.o \
	$(BUILD)/bm_filter.o $(BUILD)/bm_replay.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelbusmon

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelbusmon: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_busmon: tests/test_busmon.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_busmon.c $(OBJ) -o $@

test: $(BUILD)/test_busmon $(BUILD)/squirrelbusmon
	$(BUILD)/test_busmon
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelbusmon tests ok"

demo: $(BUILD)/squirrelbusmon
	$(BUILD)/squirrelbusmon demo

clean:
	rm -rf $(BUILD)
