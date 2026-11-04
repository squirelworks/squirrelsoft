# libs/sqfelica: FeliCa command frames and e-money history parsing (C11, no OS calls).
# Consumers add -I$(SQFELICA)/include and link $(SQFELICA)/build/libsqfelica.a.
CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Iinclude
AR ?= ar
PYTHON ?= python3
BUILD = build
FIXTURE = fixtures/parity.txt

.PHONY: all test fixture check-fixture clean

all: $(BUILD)/libsqfelica.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqfelica.o: src/sqfelica.c include/sqfelica.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqfelica.c -o $@

$(BUILD)/libsqfelica.a: $(BUILD)/sqfelica.o
	$(AR) rcs $@ $^

$(BUILD)/test_sqfelica: tests/test_sqfelica.c $(BUILD)/libsqfelica.a
	$(CC) $(CFLAGS) $< $(BUILD)/libsqfelica.a -o $@ -lm

# Regenerate the parity fixture from squirrelsuica/felica_parse.py after an
# intentional parser change on either side.
fixture:
	$(PYTHON) -I tools/gen_fixture.py $(FIXTURE)

# The committed fixture must match what felica_parse.py produces today.
check-fixture: | $(BUILD)
	$(PYTHON) -I tools/gen_fixture.py $(BUILD)/parity.txt
	diff -u $(FIXTURE) $(BUILD)/parity.txt

test: $(BUILD)/test_sqfelica check-fixture
	$(BUILD)/test_sqfelica $(FIXTURE)

clean:
	rm -rf $(BUILD)
