# SquirrelSuica: Android app (android/), Python reader (suica_read.py) and the
# C desktop reader (desktop/) on the shared FeliCa core in libs/sqfelica.
CC ?= gcc
SQFELICA = ../libs/sqfelica
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Idesktop/include -I$(SQFELICA)/include
SQS_VERSION := $(shell sed -n 's/^ *versionName *= *"\([^"]*\)".*/\1/p' android/app/build.gradle.kts | head -1)
CFLAGS += -DSQS_VERSION='"$(SQS_VERSION)"'
BUILD = build
# Live readers over PC/SC: pcsc-lite (Linux), PCSC.framework (macOS), winscard (Windows).
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
PCSC ?= macos
else ifeq ($(OS),Windows_NT)
PCSC ?= windows
else
PCSC ?= $(shell pkg-config --exists libpcsclite && echo pcsclite || echo none)
endif
ifeq ($(PCSC),pcsclite)
CFLAGS += $(shell pkg-config --cflags libpcsclite)
LDLIBS += $(shell pkg-config --libs libpcsclite)
PCSC_OBJ = $(BUILD)/transport_pcsc.o
else ifeq ($(PCSC),macos)
LDLIBS += -framework PCSC
PCSC_OBJ = $(BUILD)/transport_pcsc.o
else ifeq ($(PCSC),windows)
LDLIBS += -lwinscard
PCSC_OBJ = $(BUILD)/transport_pcsc.o
else
PCSC_OBJ = $(BUILD)/transport_nopcsc.o
endif
OBJ = $(BUILD)/sqjson.o $(BUILD)/sqcard.o $(BUILD)/virtcard.o $(BUILD)/pcsc_apdu.o $(PCSC_OBJ)
HDR = $(wildcard desktop/include/*.h) $(SQFELICA)/include/sqfelica.h
SQFELICA_LIB = $(SQFELICA)/build/libsqfelica.a

.PHONY: all desktop test test-desktop test-pcsc clean android android-test $(SQFELICA_LIB)

all: desktop
	@echo "squirrelsuica: build/squirrelsuica (card on a PC/SC reader), build/squirrelsuica --dump card.json, or python3 suica_read.py --demo"

desktop: $(BUILD)/squirrelsuica

$(BUILD):
	mkdir -p $(BUILD)

$(SQFELICA_LIB):
	$(MAKE) -C $(SQFELICA) build/libsqfelica.a

$(BUILD)/%.o: desktop/src/%.c $(HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelsuica: desktop/src/main.c $(OBJ) $(SQFELICA_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQFELICA_LIB) -o $@ $(LDLIBS)

$(BUILD)/test_units: desktop/tests/test_units.c $(OBJ) $(SQFELICA_LIB) | $(BUILD)
	$(CC) $(CFLAGS) $< $(OBJ) $(SQFELICA_LIB) -o $@ $(LDLIBS)

test-desktop: $(BUILD)/test_units $(BUILD)/squirrelsuica
	$(BUILD)/test_units
	python3 -I desktop/tests/test_desktop.py $(BUILD)/squirrelsuica
	$(MAKE) test-pcsc

# Live read through pcscd and the vpcd virtual reader (skipped when they are
# not there; PCSC_TEST_FLAGS=--require makes that a failure, as in CI).
test-pcsc: $(BUILD)/squirrelsuica
	python3 -I desktop/tests/test_pcsc.py $(BUILD)/squirrelsuica $(PCSC_TEST_FLAGS)

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
