# SquirrelSuica: Android app (android/), Python reader (suica_read.py) and the
# C desktop reader (desktop/) on the shared FeliCa core in libs/sqfelica.
CC ?= gcc
SQFELICA = ../libs/sqfelica
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Idesktop/include -I$(SQFELICA)/include
SQS_VERSION := $(shell sed -n 's/^ *versionName *= *"\([^"]*\)".*/\1/p' android/app/build.gradle.kts | head -1)
CFLAGS += -DSQS_VERSION='"$(SQS_VERSION)"'
BUILD = build
OBJ = $(BUILD)/sqjson.o $(BUILD)/sqcard.o $(BUILD)/virtcard.o
HDR = $(wildcard desktop/include/*.h) $(SQFELICA)/include/sqfelica.h
SQFELICA_LIB = $(SQFELICA)/build/libsqfelica.a

.PHONY: all desktop test test-desktop clean android android-test $(SQFELICA_LIB)

all: desktop
	@echo "squirrelsuica: build/squirrelsuica --dump card.json, or python3 suica_read.py --demo"

desktop: $(BUILD)/squirrelsuica

$(BUILD):
	mkdir -p $(BUILD)

$(SQFELICA_LIB):
	$(MAKE) -C $(SQFELICA) build/libsqfelica.a

$(BUILD)/%.o: desktop/src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelsuica: desktop/src/main.c $(OBJ) $(SQFELICA_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQFELICA_LIB) -o $@

$(BUILD)/test_units: desktop/tests/test_units.c $(OBJ) $(SQFELICA_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQFELICA_LIB) -o $@

test-desktop: $(BUILD)/test_units $(BUILD)/squirrelsuica
	$(BUILD)/test_units
	python3 -I desktop/tests/test_desktop.py $(BUILD)/squirrelsuica

test: test-desktop
	python3 tests/test_suica_parse.py
	python3 suica_read.py --demo | grep -q 'Balance'
	python3 suica_read.py --demo --json | grep -q balance_yen
	@echo "squirrelsuica tests ok"

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	@rm -rf $(BUILD) __pycache__ tests/__pycache__ desktop/tests/__pycache__ dist
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
