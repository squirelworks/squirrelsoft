# SquirrelRamTest

**Status: Soft #182 scaffold** (SPD show + fixture self-test + pass/fail artifact)

Soft companion for **Eric’s ISM RAM modules**: SPD summary and synthetic bank
self-tests in fixture mode. Hardware SoT is
[`international-squirrel-machines/memory/`](https://github.com/squirelworks/international-squirrel-machines/tree/main/memory)
(ISM #110 / #156). Soft does **not** design modules or run live I2C/PSRAM yet
(that remains S8.4b). `squirrelismbench/` `memory-test` stays the HID/device
bench harness; this app is the companion self-test utility UI/CLI.

```
make -C squirrelramtest
make -C squirrelramtest test
./build/squirrelramtest demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `show [--iface FILE]` | Print part / SPD / Soft XOR contract from fixture |
| `selftest [--iface FILE] [--report FILE]` | Run fixture patterns; write pass/fail artifact |
| `demo` | show + selftest + `build/demo-ramtest-report.txt` |

## Related

- Soft harness (`memory-test`): `squirrelismbench/`
- Keyboard configurator: Soft #180 (`squirrelkbdcfg/`)
- Mouse configurator: Soft #181 (`squirrelmousecfg/`)
- Live I2C SPD / PSRAM: ROADMAP S8.4b (gap issues)
