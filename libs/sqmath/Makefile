# libs/sqmath: expression engine and CAS core for SquirrelCalc (C11, no OS calls, no allocation).
# Consumers add -I$(SQMATH)/include and link $(SQMATH)/build/libsqmath.a -lm.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude
AR ?= ar
BUILD = build
OBJ = $(BUILD)/sqmath.o $(BUILD)/sqmath_sym.o $(BUILD)/sqmath_num.o $(BUILD)/sqmath_poly.o $(BUILD)/sqmath_integ.o
HDR = include/sqmath.h src/sqmath_internal.h

.PHONY: all test clean

all: $(BUILD)/libsqmath.a $(BUILD)/sqcalc

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libsqmath.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test_sqmath: tests/test_sqmath.c $(BUILD)/libsqmath.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqmath.a -lm -o $@

# A command-line calculator over the library: build/sqcalc --help
$(BUILD)/sqcalc: tools/sqcalc.c $(BUILD)/libsqmath.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqmath.a -lm -o $@

test: $(BUILD)/test_sqmath $(BUILD)/sqcalc
	$(BUILD)/test_sqmath
	sh tests/sqcalc_smoke.sh $(BUILD)/sqcalc

clean:
	rm -rf $(BUILD)
