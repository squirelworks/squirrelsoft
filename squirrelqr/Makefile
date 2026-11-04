.PHONY: all test serve clean android android-test

all:
	@echo "squirrelqr: android/ QR code reader (web-demo/ is an unsupported browser demo)"

test:
	python3 tests/test_web_static.py
	@echo "squirrelqr tests ok"

serve:
	python3 -m http.server 8765 --directory web-demo

android:
	cd android && ./gradlew :app:assembleDebug

android-test:
	cd android && ./gradlew :app:testDebugUnitTest

clean:
	@cd android && ./gradlew clean >/dev/null 2>&1 || true
