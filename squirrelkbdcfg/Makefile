# SquirrelKbdCfg: ISM keyboard layout + firmware configurator (Eric Soft #180).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/kb_iface.o $(BUILD)/kb_layout.o $(BUILD)/kb_cfg.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelkbdcfg

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelkbdcfg: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_kbdcfg: tests/test_kbdcfg.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_kbdcfg.c $(OBJ) -o $@

test: $(BUILD)/test_kbdcfg $(BUILD)/squirrelkbdcfg
	$(BUILD)/test_kbdcfg
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelkbdcfg tests ok"

demo: $(BUILD)/squirrelkbdcfg
	$(BUILD)/squirrelkbdcfg demo

clean:
	rm -rf $(BUILD)
