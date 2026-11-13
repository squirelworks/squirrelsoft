CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build

.PHONY: all test clean

all: $(BUILD)/squirrelgnss $(BUILD)/test_sqgnss

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqgnss.o: src/sqgnss.c include/sqgnss.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqgnss.c -o $@

$(BUILD)/squirrelgnss: src/main.c include/sqgnss.h $(BUILD)/sqgnss.o | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(BUILD)/sqgnss.o -o $@ $(LDFLAGS)

$(BUILD)/test_sqgnss: tests/test_sqgnss.c include/sqgnss.h $(BUILD)/sqgnss.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqgnss.c $(BUILD)/sqgnss.o -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqgnss $(BUILD)/squirrelgnss
	$(BUILD)/test_sqgnss
	sh tests/cli_smoke.sh $(BUILD)

clean:
	rm -rf $(BUILD)
