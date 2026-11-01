# Top-level squirrelsoft build
PROGRAMS = squirrelstation squirrelcad squirrelgnss squirrelsound
PY_PROGRAMS = squirrelqr squirrelsuica

.PHONY: all test clean $(PROGRAMS) $(PY_PROGRAMS)

all: $(PROGRAMS)
	@echo "Also: squirrelqr (web) and squirrelsuica (python) — run make test"

$(PROGRAMS):
	$(MAKE) -C $@

test:
	@for p in $(PROGRAMS); do $(MAKE) -C $$p test || exit 1; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p test || exit 1; done

clean:
	@for p in $(PROGRAMS); do $(MAKE) -C $$p clean; done
	@for p in $(PY_PROGRAMS); do $(MAKE) -C $$p clean; done
