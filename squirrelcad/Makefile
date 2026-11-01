CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build

.PHONY: all test clean

all: $(BUILD)/squirrelcad $(BUILD)/test_sqcad

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqcad.o: src/sqcad.c include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqcad.c -o $@

$(BUILD)/squirrelcad: src/main.c $(BUILD)/sqcad.o | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(BUILD)/sqcad.o -o $@ $(LDFLAGS)

$(BUILD)/test_sqcad: tests/test_sqcad.c $(BUILD)/sqcad.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqcad.c $(BUILD)/sqcad.o -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqcad $(BUILD)/squirrelcad
	$(BUILD)/test_sqcad
	@tmp=$$(mktemp /tmp/sqcXXXXXX.sqdwg); \
	$(BUILD)/squirrelcad new $$tmp && \
	$(BUILD)/squirrelcad LINE $$tmp 0,0 3,4 && \
	$(BUILD)/squirrelcad LEN $$tmp 2 | grep -q 5 && \
	rm -f $$tmp && echo "squirrelcad cli smoke ok"

clean:
	rm -rf $(BUILD)
