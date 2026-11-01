.PHONY: all test clean

all:
	@echo "squirrelsuica: python3 suica_read.py --demo"

test:
	python3 tests/test_suica_parse.py
	python3 suica_read.py --demo | grep -q 'Balance'
	python3 suica_read.py --demo --json | grep -q balance_yen
	@echo "squirrelsuica tests ok"

clean:
	@rm -rf __pycache__ tests/__pycache__
