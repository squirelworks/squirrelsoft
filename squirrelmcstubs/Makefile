# SquirrelMCStubs: Soft mission-computer stubs for fighter ICD (not flight software).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/mc_catalog.o $(BUILD)/mc_stub.o $(BUILD)/mc_sms.o $(BUILD)/mc_display.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelmcstubs

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelmcstubs: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_mcstubs: tests/test_mcstubs.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_mcstubs.c $(OBJ) -o $@

test: $(BUILD)/test_mcstubs $(BUILD)/squirrelmcstubs
	$(BUILD)/test_mcstubs
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelmcstubs tests ok"

demo: $(BUILD)/squirrelmcstubs
	$(BUILD)/squirrelmcstubs demo

clean:
	rm -rf $(BUILD)
