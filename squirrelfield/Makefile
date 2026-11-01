.PHONY: all test serve clean android android-test

all:
	@echo "squirrelfield: web/ PWA + android/ phone app"

test:
	python3 tests/test_web_static.py
	@echo "squirrelfield tests ok"

serve:
	python3 -m http.server 8765 --directory web

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
