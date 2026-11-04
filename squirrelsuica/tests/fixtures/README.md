# Made-up debugging dumps

**Real card data never goes into this repo, in any form**: not raw dumps,
not anonymized or edited copies, and not values taken from them. Real dumps
stay on your own machine as local test input.

These files are invented from scratch by `make_demo_dumps.py`. The IDm, PMm,
dates, fares, balances, shops, bus stops and terminal IDs all come from fixed
rules and a fixed random seed. Station codes are picked from the public table
in `../../data/StationCode.csv`, so names resolve.

| File | Card | Contents |
|------|------|----------|
| `demo-suica.json` | Suica-family transit card, systems `0003` and `FE00` | 20 history records (`090F`): JR and private-line rides, bus rides, shop sales on terminal types `C7`, `C8` and `CA`, a vending machine, top-ups; attributes (`008B`) with the balance; gate history (`108F`); keyed services that don't read |
| `demo-nanaco.json` | nanaco, systems `04C7` and `FE00` | balance (`5597`), 5 history records (`564F`): a top-up, purchases and a points exchange; keyed services that don't read |

Each card is a simulated FeliCa card (Polling, Request System Code, Search
Service Code, Request Service, Request Response and Read Without Encryption,
with a per-read block limit) walked by SquirrelSuicaAnalysis's own analyzer,
so the files have exactly the `squirrel-felica-dump/1` shape the app exports.

They are used by:

- `squirrelsuica/tests/test_suica_parse.py` (Python parser),
- `squirrelsuica/desktop/tests/test_desktop.py` (C desktop reader against the Python parser),
- `libs/sqfelica` (`tools/gen_fixture.py` puts their history blocks in the C/Python parity fixture),
- `squirrelsuica-analysis/tests/test_suica_analyze.py` (schema check).

They also make handy demo input:

```bash
make -C squirrelsuica desktop
squirrelsuica/build/squirrelsuica --dump squirrelsuica/tests/fixtures/demo-suica.json
```

To change them, edit `make_demo_dumps.py` and run it (from `squirrelsuica/`):
`python3 -I tests/fixtures/make_demo_dumps.py`. `make test` fails if the
committed files don't match the script.
