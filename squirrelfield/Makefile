.PHONY: all test serve clean

all:
	@echo "squirrelfield is a static web app under web/"

test:
	python3 tests/test_web_static.py
	@echo "squirrelfield tests ok"

serve:
	python3 -m http.server 8765 --directory web

clean:
	@true
