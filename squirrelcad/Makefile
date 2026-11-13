CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build
OBJ = $(BUILD)/sqcad.o $(BUILD)/sqc_dxf.o $(BUILD)/sqc_cmd.o $(BUILD)/sqc_plot.o

.PHONY: all test clean

all: $(BUILD)/squirrelcad $(BUILD)/test_sqcad $(BUILD)/test_dxf $(BUILD)/test_cmd

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/sqcad.o: src/sqcad.c include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqcad.c -o $@

$(BUILD)/sqc_dxf.o: src/sqc_dxf.c include/sqc_dxf.h include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_dxf.c -o $@

$(BUILD)/sqc_cmd.o: src/sqc_cmd.c include/sqc_cmd.h include/sqcad.h include/sqc_dxf.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_cmd.c -o $@

$(BUILD)/sqc_plot.o: src/sqc_plot.c include/sqcad.h | $(BUILD)
	$(CC) $(CFLAGS) -c src/sqc_plot.c -o $@

$(BUILD)/squirrelcad: src/main.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqcad: tests/test_sqcad.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqcad.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_dxf: tests/test_dxf.c $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_dxf.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_cmd: tests/test_cmd.c include/sqc_cmd.h $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_cmd.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqcad $(BUILD)/test_dxf $(BUILD)/test_cmd $(BUILD)/squirrelcad
	$(BUILD)/test_sqcad
	$(BUILD)/test_cmd
	sh tests/cli_smoke.sh $(BUILD)
	cd $(CURDIR) && $(BUILD)/test_dxf
	@tmp=$$(mktemp /tmp/sqcXXXXXX.sqdwg); \
	$(BUILD)/squirrelcad new $$tmp && \
	$(BUILD)/squirrelcad LINE $$tmp 0,0 3,4 && \
	$(BUILD)/squirrelcad LEN $$tmp 2 | grep -q 5 && \
	$(BUILD)/squirrelcad IMPORT-DXF /tmp/sqc_from_dxf.sqdwg samples/autocad_bridge.dxf && \
	$(BUILD)/squirrelcad LIST /tmp/sqc_from_dxf.sqdwg | grep -q ents= && \
	$(BUILD)/squirrelcad EXPORT-DXF /tmp/sqc_from_dxf.sqdwg /tmp/sqc_out.dxf && \
	$(BUILD)/squirrelcad PROBE samples/autocad_bridge.dxf | grep -q dxf && \
	rm -f $$tmp && echo "squirrelcad cli+dxf smoke ok"

clean:
	rm -rf $(BUILD)
