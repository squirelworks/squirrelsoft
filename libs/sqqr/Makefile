# libs/sqqr: QR Code decoding core (C11, no OS calls, no allocation).
# Consumers add -I$(SQQR)/include and link $(SQQR)/build/libsqqr.a.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude
AR ?= ar
PYTHON ?= python3
BUILD = build
FIXTURE = fixtures/symbols.txt

.PHONY: all test fixture check-tables clean

all: $(BUILD)/libsqqr.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqqr.o: src/sqqr.c include/sqqr.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqqr.c -o $@

$(BUILD)/libsqqr.a: $(BUILD)/sqqr.o
	$(AR) rcs $@ $^

$(BUILD)/test_sqqr: tests/test_sqqr.c $(BUILD)/libsqqr.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqqr.a -o $@

# Regenerate the fixture symbols (needs: pip install segno). The committed
# fixture is what CI tests, so CI does not need segno.
fixture:
	$(PYTHON) -I tools/gen_fixtures.py $(FIXTURE)

# Cross-check the EC block table against segno (needs segno).
check-tables:
	$(PYTHON) -I tools/gen_fixtures.py --check-tables src/sqqr.c

test: $(BUILD)/test_sqqr
	$(BUILD)/test_sqqr $(FIXTURE)

clean:
	rm -rf $(BUILD)
