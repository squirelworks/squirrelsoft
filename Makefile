# Top-level squirrelsoft build
PROGRAMS = squirrelstation squirrelcad squirrelgnss squirrelsound

.PHONY: all test clean $(PROGRAMS)

all: $(PROGRAMS)

$(PROGRAMS):
	$(MAKE) -C $@

test:
	@for p in $(PROGRAMS); do $(MAKE) -C $$p test || exit 1; done

clean:
	@for p in $(PROGRAMS); do $(MAKE) -C $$p clean; done
