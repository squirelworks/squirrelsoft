# SquirrelISMBench: Soft test harness for ISM consumer HID + lab devices.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
SQN = ../libs/squirrelnum
CFLAGS += -Iinclude -I$(SQN)/include
LDFLAGS ?= -lm
BUILD = build
SQNLIB = $(SQN)/build/libsquirrelnum.a

OBJ = $(BUILD)/ib_iface.o $(BUILD)/ib_keyboard.o $(BUILD)/ib_mouse.o \
	$(BUILD)/ib_memory.o $(BUILD)/ib_printer.o $(BUILD)/ib_plotter.o \
	$(BUILD)/ib_power.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo $(SQNLIB)

all: $(BUILD)/squirrelismbench

$(BUILD):
	mkdir -p $(BUILD)

$(SQNLIB):
	$(MAKE) -C $(SQN)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelismbench: src/main.c $(OBJ) $(SQNLIB) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) $(SQNLIB) -o $@ $(LDFLAGS)

$(BUILD)/test_ismbench: tests/test_ismbench.c $(OBJ) $(SQNLIB) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_ismbench.c $(OBJ) $(SQNLIB) -o $@ $(LDFLAGS)

test: $(BUILD)/test_ismbench $(BUILD)/squirrelismbench
	$(BUILD)/test_ismbench
	sh tests/cli_smoke.sh $(BUILD)
	@echo "squirrelismbench tests ok"

demo: $(BUILD)/squirrelismbench
	$(BUILD)/squirrelismbench demo

clean:
	rm -rf $(BUILD)
