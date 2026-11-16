# SquirrelISMBench: Soft test harness for ISM consumer HID devices.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/ib_iface.o $(BUILD)/ib_keyboard.o $(BUILD)/ib_mouse.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelismbench

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelismbench: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_ismbench: tests/test_ismbench.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_ismbench.c $(OBJ) -o $@

test: $(BUILD)/test_ismbench $(BUILD)/squirrelismbench
	$(BUILD)/test_ismbench
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelismbench tests ok"

demo: $(BUILD)/squirrelismbench
	$(BUILD)/squirrelismbench demo

clean:
	rm -rf $(BUILD)
