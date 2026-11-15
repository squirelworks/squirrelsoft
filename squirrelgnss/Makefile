CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -Iinclude
LDFLAGS ?= -lm
BUILD = build
OBJ = $(BUILD)/sqgnss.o $(BUILD)/sqrinex.o $(BUILD)/sqsurvey.o $(BUILD)/sqrtk.o \
	$(BUILD)/sqrecv.o $(BUILD)/sqserial.o $(BUILD)/sqntrip.o $(BUILD)/sqntrip_transport.o \
	$(BUILD)/sqrtcm.o

UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IS_WIN := $(findstring MINGW,$(UNAME_S))$(findstring MSYS,$(UNAME_S))$(findstring CYGWIN,$(UNAME_S))
ifneq ($(IS_WIN),)
LDFLAGS += -lws2_32
endif

.PHONY: all test test-jni clean android android-test

all: $(BUILD)/squirrelgnss $(BUILD)/test_sqgnss

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/sqgnss.h include/sqrinex.h include/sqrtk.h include/sqrecv.h \
		include/sqntrip.h include/sqrtcm.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/squirrelgnss: src/main.c include/sqgnss.h include/sqrecv.h include/sqntrip.h \
		include/sqrtcm.h $(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) src/main.c $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/test_sqgnss: tests/test_sqgnss.c tests/test_rinex.c tests/test_rtk.c tests/test_recv.c \
		tests/test_ntrip.c tests/test_rtcm.c include/sqgnss.h include/sqntrip.h include/sqrtcm.h \
		$(OBJ) | $(BUILD)
	$(CC) $(CFLAGS) tests/test_sqgnss.c tests/test_rinex.c tests/test_rtk.c tests/test_recv.c \
		tests/test_ntrip.c tests/test_rtcm.c $(OBJ) -o $@ $(LDFLAGS)

test: $(BUILD)/test_sqgnss $(BUILD)/squirrelgnss test-jni
	$(BUILD)/test_sqgnss
	sh tests/cli_smoke.sh $(BUILD)

# The Android JNI bridge built for this machine and driven from a JVM
# (android/jni-smoke): skipped when there is no JDK. Synthetic measurement
# feed (fillDemoEpoch → solveEpoch) plus NTRIP memio/loopback → MSM; no
# phone GNSS and no remote caster in CI.
JDK ?= $(shell if [ -n "$$JAVA_HOME" ]; then echo "$$JAVA_HOME"; \
	elif command -v javac >/dev/null; then dirname "$$(dirname "$$(readlink -f "$$(command -v javac)")")"; fi)
JNI_SRC = android/app/src/main/cpp/sqgnss_jni.c src/sqgnss.c src/sqntrip.c \
	src/sqntrip_transport.c src/sqrtcm.c

test-jni: | $(BUILD)
	@if [ "$$(uname -s)" != Linux ]; then \
		echo "squirrelgnss: JNI smoke is Linux-only (shared .so); skipped on $$(uname -s)"; \
	elif [ -n "$(JDK)" ] && [ -f "$(JDK)/include/jni.h" ] && [ -x "$(JDK)/bin/javac" ]; then \
		set -e; plat=$$(ls -d "$(JDK)"/include/*/ | head -1); \
		$(CC) -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC -I"$(JDK)/include" -I"$$plat" \
			-Iinclude $(JNI_SRC) -lm -o $(BUILD)/libsqgnss_jni.so; \
		mkdir -p $(BUILD)/jni-smoke; \
		"$(JDK)/bin/javac" -d $(BUILD)/jni-smoke android/jni-smoke/works/squirrel/gnss/SqGnss.java; \
		"$(JDK)/bin/java" -cp $(BUILD)/jni-smoke works.squirrel.gnss.SqGnss "$$(pwd)/$(BUILD)/libsqgnss_jni.so"; \
	else echo "squirrelgnss: no JDK, JNI smoke test skipped"; fi

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	rm -rf $(BUILD)
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
