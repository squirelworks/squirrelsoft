CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build
OBJ = $(BUILD)/sqgnss.o $(BUILD)/sqrinex.o

.PHONY: all test clean

all: $(BUILD)/squirrelgnss $(BUILD)/test_sqgnss

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/sqgnss.h include/sqrinex.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelgnss: src/main.c include/sqgnss.h $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqgnss: tests/test_sqgnss.c tests/test_rinex.c include/sqgnss.h $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqgnss.c tests/test_rinex.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqgnss $(BUILD)/squirrelgnss
	$(BUILD)/test_sqgnss
	sh tests/cli_smoke.sh $(BUILD)

clean:
	rm -rf $(BUILD)
