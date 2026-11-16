# SquirrelISMBench

**Status: Active (S8.1 + S8.1b + S8.4a)**

Soft-side test harness for International Squirrel Machines **consumer** devices
(Eric). Targets: **60% keyboard** matrix/HID, **mouse** sensor/scroll/buttons,
**typewriter** 4×12 matrix (ISM #4 / PR #159), and **RAM module** SPD +
synthetic bank patterns (ISM #156 / Soft #182).

Steve / Soft owns simulators and bench tools only — not PCB or shell design.
Device interfaces are SoT in `squirelworks/international-squirrel-machines`.
Until Eric publishes machine-readable interfaces, CI uses fixtures under
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
| `typewriter-test [--iface FILE]` | 4×12 typewriter matrix self-test |
| `mouse-test [--iface FILE]` | Sensor clamp, scroll, button bits |
| `memory-test [--iface FILE]` | SPD XOR contract + walking/checker/LCG bank patterns |
| `demo` | All against fixtures |

## Numerical Recipes grounding

`memory-test` uses a Park–Miller multiplicative LCG as the **random-pattern
oracle** (NR **Ch. 7.1** class of portable generator: a=16807, m=2³¹−1). Soft
implements the algorithm originally; it does **not** copy Numerical Recipes
listings. Walking-bit and checkerboard patterns are Soft originals for CI.

## Planned (not in this slice)

- Live USB HID capture against real boards
- Live I2C SPD parse of probe CDC lines (`ISM SPD: xx …`)
- Live PSRAM/DRAM march once Soft has a host path to the module
- Soft companion UI for RAM (Soft #182, lane `-2eaf`, after kbd/mouse apps)
- PSU / UPS / remaining lab equipment benches

## Related

- ISM keyboard 60%: issue #61 · Soft needs interface #89
- ISM mouse: issue #3 · Soft needs interface #90
- ISM typewriter: issue #4 · PR #159
- ISM memory / RAM: issue #110 · merged Soft follow-up #156 · Soft #182
- Fighter Jet Flight Lab (Victor / military) is a separate Soft app:
  `squirrelflightlab/`
