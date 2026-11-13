# Top-level squirrelsoft build
LIBS = libs/sqdsp libs/sqfelica libs/sqqr libs/sqgui libs/sqmath
PROGRAMS = squirrelstation squirrelcad squirrelgnss squirrelsound squirrelqr squirrelsuica squirrelcalc squirrelpaint
PY_PROGRAMS = squirrelsuica-analysis

.PHONY: all test clean $(PROGRAMS) $(PY_PROGRAMS)

all: $(PROGRAMS)
	@echo "Also: squirrelsuica-analysis (python) — run make test"

$(PROGRAMS):
	$(MAKE) -C $@

test:
	@for p in $(LIBS); do $(MAKE) -C $$p test || exit 1; done
	@for p in $(PROGRAMS); do $(MAKE) -C $$p test || exit 1; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p test || exit 1; done

clean:
	@for p in $(LIBS); do $(MAKE) -C $$p clean; done
	@for p in $(PROGRAMS); do $(MAKE) -C $$p clean; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p clean; done
