# SquirrelRamTest

**Status: Soft #182 scaffold** (SPD show + fixture self-test + pass/fail artifact)

Soft companion for **Eric’s ISM RAM modules**: SPD summary and synthetic bank
self-tests in fixture mode. Hardware SoT is
[`international-squirrel-machines/memory/`](https://github.com/squirelworks/international-squirrel-machines/tree/main/memory)
(ISM #110 / #156). Soft does **not** design modules. Soft #203 USB-CDC `ISM SPD:` framing is
supported via `--spd-cdc` (CI uses `fixtures/memory/cdc-spd-stub.sqspd`). Live
PSRAM march remains Soft #204. `squirrelismbench/` `memory-test` stays the
HID/device bench harness; this app is the companion self-test utility UI/CLI.

```
make -C squirrelramtest
make -C squirrelramtest test
./build/squirrelramtest demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `show [--iface FILE] [--spd-cdc PATH]` | Print part / SPD / Soft XOR; optional CDC framing |
| `selftest [--iface FILE] [--spd-cdc PATH] [--report FILE]` | Run patterns; write pass/fail artifact |
| `demo` | show + selftest + `build/demo-ramtest-report.txt` |

## Related

- Soft harness (`memory-test`): `squirrelismbench/` (+ Soft #203 `docs/spd-cdc.md`)
- Keyboard configurator: Soft #180 (`squirrelkbdcfg/`)
- Mouse configurator: Soft #181 (`squirrelmousecfg/`)
- Live PSRAM march: Soft #204 (ROADMAP S8.4b″)
