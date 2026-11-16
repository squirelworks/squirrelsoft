# SquirrelMouseCfg

**Status: Soft #181 scaffold** (DPI profiles + button remap + SQMS1 round-trip)

Soft companion for **Eric’s ISM mouse**: DPI steps/profiles and button mapping.
Hardware SoT is
[`international-squirrel-machines/mouse/`](https://github.com/squirelworks/international-squirrel-machines/tree/main/mouse)
(ISM interface #90 when published). Soft does **not** design PCBs or shells.
`squirrelismbench/` remains the HID bench harness; this app is the settings UI.

```
make -C squirrelmousecfg
make -C squirrelmousecfg test
./build/squirrelmousecfg demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `show [--iface FILE]` | Print DPI steps/profile and button map from fixture |
| `dpi-set CPI` | Select active DPI from known steps (or add then select) |
| `button-map PHYS ACT` | Remap physical button (`left`/`right`/`middle`/`side1`/`side2`) |
| `cfg-write FILE` | Write Soft mouse-config blob |
| `cfg-read FILE` | Reload Soft config blob and verify round-trip |
| `demo` | show + dpi/button edits + write/read temp cfg |

## Related

- Soft harness (sensor/scroll/button self-test): `squirrelismbench/`
- Keyboard configurator: Soft #180 (`squirrelkbdcfg/`)
- RAM self-test (later): Soft #182
- Fighter Flight Lab is Steve’s app (`squirrelflightlab/`) — not duplicated here
