CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm

BUILD = build
OBJ = $(BUILD)/sqstation.o $(BUILD)/sqs_dxf.o

.PHONY: all test clean

all: $(BUILD)/squirrelstation $(BUILD)/test_sqstation $(BUILD)/test_dxf $(BUILD)/test_elements

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqstation.o: src/sqstation.c include/sqstation.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqstation.c -o $@

$(BUILD)/sqs_dxf.o: src/sqs_dxf.c include/sqs_dxf.h include/sqstation.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqs_dxf.c -o $@

$(BUILD)/squirrelstation: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqstation: tests/test_sqstation.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqstation.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_dxf: tests/test_dxf.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_dxf.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_elements: tests/test_elements.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_elements.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqstation $(BUILD)/test_dxf $(BUILD)/test_elements $(BUILD)/squirrelstation
	$(BUILD)/test_sqstation
	$(BUILD)/test_elements
	sh tests/cli_smoke.sh $(BUILD)
	cd $(CURDIR) && $(BUILD)/test_dxf
	@tmp=$$(mktemp /tmp/sqsXXXXXX.sqdgn); dxf=samples/microstation_bridge.dxf; \
	$(BUILD)/squirrelstation import-dxf $$tmp $$dxf && \
	$(BUILD)/squirrelstation list $$tmp | grep -q LINE && \
	$(BUILD)/squirrelstation export-dxf $$tmp /tmp/sqs_cli.dxf && \
	$(BUILD)/squirrelstation probe $$dxf | grep -q dxf && \
	rm -f $$tmp && echo "squirrelstation cli+dxf smoke ok"

clean:
	rm -rf $(BUILD)
