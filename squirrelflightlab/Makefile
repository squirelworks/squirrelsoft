# SquirrelFlightLab: ISM fighter-jet flight lab (desktop scaffold).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
SQN = ../libs/squirrelnum
CFLAGS += -Iinclude -I$(SQN)/include
LDFLAGS ?= -lm
BUILD = build
SQNLIB = $(SQN)/build/libsquirrelnum.a

OBJ = $(BUILD)/fl_project.o $(BUILD)/fl_aero.o $(BUILD)/fl_dyn.o \
	$(BUILD)/fl_avionics.o $(BUILD)/fl_scenario.o $(BUILD)/fl_planned.o \
	$(BUILD)/fl_ism.o $(BUILD)/fl_fcs.o $(BUILD)/fl_yaml.o $(BUILD)/fl_iface.o
HDR = $(wildcard include/*.h)

.PHONY: all test clean demo $(SQNLIB)

all: $(BUILD)/squirrelflightlab

$(BUILD):
	mkdir -p $(BUILD)

$(SQNLIB):
	$(MAKE) -C $(SQN)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelflightlab: src/main.c $(OBJ) $(SQNLIB) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) $(SQNLIB) -o $@ $(LDFLAGS)

$(BUILD)/test_flightlab: tests/test_flightlab.c $(OBJ) $(SQNLIB) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_flightlab.c $(OBJ) $(SQNLIB) -o $@ $(LDFLAGS)

test: $(BUILD)/test_flightlab $(BUILD)/squirrelflightlab
	$(BUILD)/test_flightlab
	sh tests/cli_smoke.sh $(BUILD)
	sh scripts/demo_happy_path.sh $(BUILD)
	@echo "squirrelflightlab tests ok"

demo: $(BUILD)/squirrelflightlab
	sh scripts/demo_happy_path.sh $(BUILD)

clean:
	rm -rf $(BUILD)
