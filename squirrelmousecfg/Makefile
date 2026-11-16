# SquirrelMouseCfg: ISM mouse DPI + button configurator (Eric Soft #181).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
BUILD = build

OBJ = $(BUILD)/ms_iface.o $(BUILD)/ms_dpi.o $(BUILD)/ms_btn.o $(BUILD)/ms_cfg.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelmousecfg

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelmousecfg: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@

$(BUILD)/test_mousecfg: tests/test_mousecfg.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_mousecfg.c $(OBJ) -o $@

test: $(BUILD)/test_mousecfg $(BUILD)/squirrelmousecfg
	$(BUILD)/test_mousecfg
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelmousecfg tests ok"

demo: $(BUILD)/squirrelmousecfg
	$(BUILD)/squirrelmousecfg demo

clean:
	rm -rf $(BUILD)
