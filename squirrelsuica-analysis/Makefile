.PHONY: all test test-jni clean

SQFELICA = ../libs/sqfelica
BUILD = build
CC ?= gcc

# The Android JNI bridge built for this machine and driven from a JVM
# (android/jni-smoke): skipped when there is no JDK.
JDK ?= $(shell if [ -n "$$JAVA_HOME" ]; then echo "$$JAVA_HOME"; \
	elif command -v javac >/dev/null; then dirname "$$(dirname "$$(readlink -f "$$(command -v javac)")")"; fi)

all:
	@echo "squirrelsuica-analysis: python3 suica_analyze.py --demo | android: cd android && ./gradlew :app:assembleDebug"

$(BUILD):
	mkdir -p $(BUILD)

test: test-jni
	python3 tests/test_suica_analyze.py
	python3 suica_analyze.py --demo --redact | grep -q '"schema": "squirrel-felica-dump/1"'
	@echo "squirrelsuica-analysis tests ok"

test-jni: | $(BUILD)
	@if [ "$$(uname -s)" != Linux ]; then \
		echo "squirrelsuica-analysis: JNI smoke is Linux-only (shared .so); skipped on $$(uname -s)"; \
	elif [ -n "$(JDK)" ] && [ -f "$(JDK)/include/jni.h" ] && [ -x "$(JDK)/bin/javac" ]; then \
		set -e; plat=$$(ls -d "$(JDK)"/include/*/ | head -1); \
		$(CC) -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC \
			-I"$(JDK)/include" -I"$$plat" -I$(SQFELICA)/include \
			android/app/src/main/cpp/sqfelica_jni.c $(SQFELICA)/src/sqfelica.c \
			-o $(BUILD)/libsqfelica_jni.so; \
		mkdir -p $(BUILD)/jni-smoke; \
		"$(JDK)/bin/javac" -d $(BUILD)/jni-smoke \
			android/jni-smoke/works/squirrel/suicaanalysis/SqFelica.java; \
		"$(JDK)/bin/java" -cp $(BUILD)/jni-smoke \
			works.squirrel.suicaanalysis.SqFelica "$$(pwd)/$(BUILD)/libsqfelica_jni.so"; \
	else echo "squirrelsuica-analysis: no JDK, JNI smoke test skipped"; fi

clean:
	@rm -rf __pycache__ tests/__pycache__ $(BUILD)
