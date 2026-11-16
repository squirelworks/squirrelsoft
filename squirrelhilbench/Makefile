# SquirrelHILBench: Soft HIL bench UI for ISM fighter (Victor).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
LDFLAGS ?= -lm
BUILD = build

OBJ = $(BUILD)/hb_profile.o $(BUILD)/hb_health.o $(BUILD)/hb_stimulus.o $(BUILD)/hb_log.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo

all: $(BUILD)/squirrelhilbench

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelhilbench: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_hilbench: tests/test_hilbench.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_hilbench.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_hilbench $(BUILD)/squirrelhilbench
	$(BUILD)/test_hilbench
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelhilbench tests ok"

demo: $(BUILD)/squirrelhilbench
	$(BUILD)/squirrelhilbench demo

clean:
	rm -rf $(BUILD)
