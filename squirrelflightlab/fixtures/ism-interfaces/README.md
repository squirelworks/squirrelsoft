# ISM fighter-jet/interfaces fixtures (Soft #170)

Vendored snapshot of `international-squirrel-machines` `fighter-jet/interfaces/`
from ISM `main` for CI. Soft does **not** fork the SoT — refresh by copying
from the ISM tree when Victor publishes schema changes:

```sh
# from a checkout of squirelworks/international-squirrel-machines
cp fighter-jet/interfaces/*.yaml \
  path/to/squirrelsoft/squirrelflightlab/fixtures/ism-interfaces/
# then refresh Soft allowlists if top-level or Soft-cared nested keys changed:
#   squirrelflightlab/schemas/ism-fj-1.0.0/top-level-allowlist.yaml
# Soft #177: optional nested: { path: { required: [...], allowed: [...] } }
# with dotted paths and [] for sequence-of-maps (e.g. surfaces[], envelope).
```

Load with:

```sh
./build/squirrelflightlab ism-interfaces fixtures/ism-interfaces
```

`README.ism.md` is the ISM interfaces README copied alongside for provenance.
