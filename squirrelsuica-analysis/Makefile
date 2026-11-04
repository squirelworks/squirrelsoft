.PHONY: all test clean

all:
	@echo "squirrelsuica-analysis: python3 suica_analyze.py --demo | android: cd android && ./gradlew :app:assembleDebug"

test:
	python3 tests/test_suica_analyze.py
	python3 suica_analyze.py --demo --redact | grep -q '"schema": "squirrel-felica-dump/1"'
	@echo "squirrelsuica-analysis tests ok"

clean:
	@rm -rf __pycache__ tests/__pycache__
