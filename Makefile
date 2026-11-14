# Top-level squirrelsoft build
LIBS = libs/sqdsp libs/sqfelica libs/sqqr libs/sqgui libs/sqmath
PROGRAMS = squirrelstation squirrelcad squirrelgnss squirrelsound squirrelqr squirrelsuica squirrelcalc squirrelpaint squirrelgames squirreldebug
PY_PROGRAMS = squirrelsuica-analysis

.PHONY: all test docs-lint clean $(PROGRAMS) $(PY_PROGRAMS)

all: $(PROGRAMS)
	@echo "Also: squirrelsuica-analysis (python) — run make test"

$(PROGRAMS):
	$(MAKE) -C $@

# CONTINUANCE §3c: ROADMAP/TODO checked boxes that name a check must find it
# in the make-test harness; open boxes must not claim a check that exists.
docs-lint:
	python3 tools/docs_lint.py

test: docs-lint
	@for p in $(LIBS); do $(MAKE) -C $$p test || exit 1; done
	@for p in $(PROGRAMS); do $(MAKE) -C $$p test || exit 1; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p test || exit 1; done

clean:
	@for p in $(LIBS); do $(MAKE) -C $$p clean; done
	@for p in $(PROGRAMS); do $(MAKE) -C $$p clean; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p clean; done
