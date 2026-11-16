# SquirrelShipBench: Soft harness for Victor warship / bomber Soft stubs.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
LDFLAGS ?= -lm
BUILD = build

OBJ = $(BUILD)/sb_hydro.o $(BUILD)/sb_board.o $(BUILD)/sb_aero.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelshipbench

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelshipbench: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_shipbench: tests/test_shipbench.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_shipbench.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_shipbench $(BUILD)/squirrelshipbench
	$(BUILD)/test_shipbench
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelshipbench tests ok"

demo: $(BUILD)/squirrelshipbench
	$(BUILD)/squirrelshipbench demo

clean:
	rm -rf $(BUILD)
