# SquirrelFlightLab: ISM fighter-jet flight lab (desktop scaffold).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
SQN = ../libs/squirrelnum
SQGUI = ../libs/sqgui
CFLAGS += -Iinclude -I$(SQN)/include -I$(SQGUI)/include
LDFLAGS ?= -lm
BUILD = build
SQNLIB = $(SQN)/build/libsquirrelnum.a
SQGUILIB = $(SQGUI)/build/libsqgui.a

OBJ = $(BUILD)/fl_project.o $(BUILD)/fl_aero.o $(BUILD)/fl_dyn.o \
	$(BUILD)/fl_avionics.o $(BUILD)/fl_scenario.o $(BUILD)/fl_planned.o \
	$(BUILD)/fl_ism.o $(BUILD)/fl_fcs.o $(BUILD)/fl_yaml.o $(BUILD)/fl_iface.o \
	$(BUILD)/fl_verify.o $(BUILD)/fl_viability.o $(BUILD)/fl_loadout.o \
	$(BUILD)/fl_chart.o $(BUILD)/fl_hil.o
HDR = $(wildcard include/*.h)

.PHONY: all test verify viability clean demo $(SQNLIB) $(SQGUILIB)

all: $(BUILD)/squirrelflightlab

$(BUILD):
	mkdir -p $(BUILD)

$(SQNLIB):
	$(MAKE) -C $(SQN)

$(SQGUILIB):
	$(MAKE) -C $(SQGUI)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelflightlab: src/main.c $(OBJ) $(SQNLIB) $(SQGUILIB) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) $(SQNLIB) $(SQGUILIB) -o $@ $(LDFLAGS)

$(BUILD)/test_flightlab: tests/test_flightlab.c $(OBJ) $(SQNLIB) $(SQGUILIB) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_flightlab.c $(OBJ) $(SQNLIB) $(SQGUILIB) -o $@ $(LDFLAGS)

test: $(BUILD)/test_flightlab $(BUILD)/squirrelflightlab
	$(BUILD)/test_flightlab
	sh tests/cli_smoke.sh $(BUILD)
	sh scripts/demo_happy_path.sh $(BUILD)
	$(MAKE) verify
	$(MAKE) viability
	@echo "squirrelflightlab tests ok"

verify: $(BUILD)/squirrelflightlab
	$(BUILD)/squirrelflightlab verify | tee $(BUILD)/verify_report.md
	@grep -q "0 fail" $(BUILD)/verify_report.md

viability: $(BUILD)/squirrelflightlab
	$(BUILD)/squirrelflightlab viability-report --json $(BUILD)/viability_report.json \
		| tee $(BUILD)/viability_report.md
	@grep -q "fail=0" $(BUILD)/viability_report.md
	@grep -q '"overall"' $(BUILD)/viability_report.json

demo: $(BUILD)/squirrelflightlab
	sh scripts/demo_happy_path.sh $(BUILD)

clean:
	rm -rf $(BUILD)
