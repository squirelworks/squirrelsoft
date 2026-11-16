# libs/squirrelnum: NR-grounded numerics (original Soft code; no NR listings).
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
AR ?= ar
BUILD = build

OBJ = $(BUILD)/sqn_rk.o $(BUILD)/sqn_brent.o $(BUILD)/sqn_interp.o \
	$(BUILD)/sqn_la.o $(BUILD)/sqn_quat.o

.PHONY: all test clean

all: $(BUILD)/libsquirrelnum.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/squirrelnum.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libsquirrelnum.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test_squirrelnum: tests/test_squirrelnum.c $(BUILD)/libsquirrelnum.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsquirrelnum.a -o $@ -lm

test: $(BUILD)/test_squirrelnum
	$(BUILD)/test_squirrelnum

clean:
	rm -rf $(BUILD)
