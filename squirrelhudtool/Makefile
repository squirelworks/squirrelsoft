# SquirrelHUDTool: Soft symbology / HUD+MFD page layout CLI (ISM #82).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/ht_page.o $(BUILD)/ht_export.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelhudtool

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelhudtool: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_hudtool: tests/test_hudtool.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_hudtool.c $(OBJ) -o $@

test: $(BUILD)/test_hudtool $(BUILD)/squirrelhudtool
	$(BUILD)/test_hudtool
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelhudtool tests ok"

demo: $(BUILD)/squirrelhudtool
	$(BUILD)/squirrelhudtool demo

clean:
	rm -rf $(BUILD)
