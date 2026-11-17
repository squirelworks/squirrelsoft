# SquirrelRamTest

**Status: Soft #182 scaffold** (SPD show + fixture self-test + pass/fail artifact)

Soft companion for **Eric’s ISM RAM modules**: SPD summary and synthetic bank
self-tests in fixture mode. Hardware SoT is
[`international-squirrel-machines/memory/`](https://github.com/squirelworks/international-squirrel-machines/tree/main/memory)
(ISM #110 / #156). Soft does **not** design modules. Soft #203 `--spd-cdc` and Soft #204
`--live-march` (Soft stub Soft bank Soft march; ACM deferred) are supported.
`squirrelismbench/` `memory-test` stays the HID/device bench harness; this app
is the companion self-test utility UI/CLI.

```
make -C squirrelramtest
make -C squirrelramtest test
./build/squirrelramtest demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `show [--iface FILE] [--spd-cdc PATH]` | Print part / SPD / Soft XOR; optional CDC framing |
| `selftest [--iface FILE] [--spd-cdc PATH] [--live-march EP] [--report FILE]` | Fixture or Soft live Soft march |
| `demo` | show + selftest + `build/demo-ramtest-report.txt` |

## Related

- Soft harness (`memory-test`): `squirrelismbench/` (`docs/spd-cdc.md`, `docs/psram-live.md`)
- Keyboard configurator: Soft #180 (`squirrelkbdcfg/`)
- Mouse configurator: Soft #181 (`squirrelmousecfg/`)

