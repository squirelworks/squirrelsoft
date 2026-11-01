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

$(BUILD)/squirrelgnss: src/main.c $(BUILD)/sqgnss.o | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(BUILD)/sqgnss.o -o $@ $(LDFLAGS)

$(BUILD)/test_sqgnss: tests/test_sqgnss.c $(BUILD)/sqgnss.o | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqgnss.c $(BUILD)/sqgnss.o -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqgnss $(BUILD)/squirrelgnss
	$(BUILD)/test_sqgnss
	$(BUILD)/squirrelgnss demo-track 3 | grep -q conv=1
	@echo "squirrelgnss cli smoke ok"

clean:
	rm -rf $(BUILD)
