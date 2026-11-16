# SquirrelISMBench

**Status: Scaffolded**

Soft-side test harness for International Squirrel Machines **consumer** devices
(Eric). First targets: **60% keyboard** matrix/HID and **mouse**
sensor/scroll/buttons.

Steve / Soft owns simulators and bench tools only — not PCB or shell design.
Device interfaces are SoT in `squirelworks/international-squirrel-machines`.
Until Eric publishes them (ISM #89 keyboard, #90 mouse), CI uses fixtures under
`fixtures/`.

```
make -C squirrelismbench
make -C squirrelismbench test
./build/squirrelismbench demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `keyboard-test [--iface FILE]` | Matrix press/release self-test + boot report path |
| `mouse-test [--iface FILE]` | Sensor clamp, scroll, button bits |
| `demo` | Both against fixtures |

## Planned (not in v0)

- Live USB HID capture against real boards
- PSU / UPS / storage / full desktop suite benches
- Stuck-key / ghosting extended suites once Eric freezes the matrix map

## Related

- ISM keyboard 60%: issue #61 · Soft needs interface #89
- ISM mouse: issue #3 · Soft needs interface #90
- Fighter Jet Flight Lab (Victor / military) is a separate Soft app:
  `squirrelflightlab/`
