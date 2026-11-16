# ISM fighter-jet/interfaces fixtures (Soft #170)

Vendored snapshot of `international-squirrel-machines` `fighter-jet/interfaces/`
from ISM `main` for CI. Soft does **not** fork the SoT — refresh by copying
from the ISM tree when Victor publishes schema changes:

```sh
# from a checkout of squirelworks/international-squirrel-machines
cp fighter-jet/interfaces/*.yaml \
  path/to/squirrelsoft/squirrelflightlab/fixtures/ism-interfaces/
# then refresh Soft allowlists if top-level keys changed:
#   squirrelflightlab/schemas/ism-fj-1.0.0/top-level-allowlist.yaml
```

Load with:

```sh
./build/squirrelflightlab ism-interfaces fixtures/ism-interfaces
```

`README.ism.md` is the ISM interfaces README copied alongside for provenance.
