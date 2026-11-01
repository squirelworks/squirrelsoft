.PHONY: all test clean android android-test

all:
	@echo "squirrelsuica: python3 suica_read.py --demo"
	@echo "android: make -C android is via ./android/gradlew"

test:
	python3 tests/test_suica_parse.py
	python3 suica_read.py --demo | grep -q 'Balance'
	python3 suica_read.py --demo --json | grep -q balance_yen
	@echo "squirrelsuica tests ok"

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	@rm -rf __pycache__ tests/__pycache__ dist
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
