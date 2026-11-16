# SquirrelKbdCfg

**Status: Soft #180 scaffold** (layout show + SQKB1 config round-trip)

Soft companion for **Eric’s ISM keyboards**: layout map editor and firmware
configurator. Hardware SoT is
[`international-squirrel-machines/keyboard/`](https://github.com/squirelworks/international-squirrel-machines/tree/main/keyboard)
(ISM interface #89 when published). Soft does **not** design PCBs or shells.

```
make -C squirrelkbdcfg
make -C squirrelkbdcfg test
./build/squirrelkbdcfg demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `show [--iface FILE]` | Print layout matrix from interface fixture |
| `cfg-write FILE` | Write Soft firmware-config blob from current iface |
| `cfg-read FILE` | Reload Soft config blob and verify round-trip |
| `demo` | show + write/read temp cfg |

## Related

- Soft harness (matrix self-test): `squirrelismbench/`
- Mouse settings (next): Soft #181
- RAM self-test (later): Soft #182
- Fighter Flight Lab is Steve’s app (`squirrelflightlab/`) — not duplicated here
