CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm

BUILD = build
SRC = src/sqstation.c
OBJ = $(BUILD)/sqstation.o

.PHONY: all test clean

all: $(BUILD)/squirrelstation $(BUILD)/test_sqstation

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqstation.o: src/sqstation.c include/sqstation.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqstation.c -o $@

$(BUILD)/squirrelstation: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqstation: tests/test_sqstation.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqstation.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqstation $(BUILD)/squirrelstation
	$(BUILD)/test_sqstation
	@tmp=$$(mktemp /tmp/sqsXXXXXX.sqdgn); \
	$(BUILD)/squirrelstation new $$tmp demo && \
	$(BUILD)/squirrelstation line $$tmp 0,0,0 10,0,0 && \
	$(BUILD)/squirrelstation list $$tmp | grep -q LINE && \
	rm -f $$tmp && echo "squirrelstation cli smoke ok"

clean:
	rm -rf $(BUILD)
