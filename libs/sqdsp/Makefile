# libs/sqdsp: SquirrelSound's spectral DSP core (C11, no OS calls).
# Consumers add -I$(SQDSP)/include and link $(SQDSP)/build/libsqdsp.a.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
CFLAGS += -Iinclude
AR ?= ar
BUILD = build
FIXTURE = fixtures/parity_1024.txt

.PHONY: all test fixture check-fixture clean

all: $(BUILD)/libsqdsp.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqdsp.o: src/sqdsp.c include/sqdsp.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqdsp.c -o $@

$(BUILD)/libsqdsp.a: $(BUILD)/sqdsp.o
	$(AR) rcs $@ $^

$(BUILD)/test_sqdsp_core: tests/test_sqdsp_core.c $(BUILD)/libsqdsp.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqdsp.a -o $@ -lm

$(BUILD)/gen_parity: tools/gen_parity.c $(BUILD)/libsqdsp.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqdsp.a -o $@ -lm

# Regenerate the Kotlin parity fixture after an intentional sqdsp change.
fixture: $(BUILD)/gen_parity
	$(BUILD)/gen_parity $(FIXTURE)

# The committed fixture must match what this sqdsp produces.
check-fixture: $(BUILD)/gen_parity
	$(BUILD)/gen_parity --check $(FIXTURE)

test: $(BUILD)/test_sqdsp_core check-fixture
	$(BUILD)/test_sqdsp_core

clean:
	rm -rf $(BUILD)
