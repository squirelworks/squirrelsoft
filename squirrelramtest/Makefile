# SquirrelRamTest: ISM RAM-module self-test utility (Eric Soft #182).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/rm_iface.o $(BUILD)/rm_spd.o $(BUILD)/rm_selftest.o $(BUILD)/rm_report.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelramtest

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelramtest: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_ramtest: tests/test_ramtest.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_ramtest.c $(OBJ) -o $@

test: $(BUILD)/test_ramtest $(BUILD)/squirrelramtest
	$(BUILD)/test_ramtest
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelramtest tests ok"

demo: $(BUILD)/squirrelramtest
	$(BUILD)/squirrelramtest demo

clean:
	rm -rf $(BUILD)
