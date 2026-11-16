# SquirrelISMBench

**Status: Active (S8.1 + S8.1b + S8.2 Soft HID + S8.3 + S8.4a + S8.4b′)**

Soft-side test harness for International Squirrel Machines **consumer** devices
(Eric): keyboard, mouse, typewriter, RAM module, **printer**, Soft-proposed
**plotter**, Soft S8.3 **PSU / UPS**, and Soft S8.2 **mock USB HID** framing.

```
make -C squirrelismbench
make -C squirrelismbench test
./build/squirrelismbench demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `keyboard-test` | Matrix press/release self-test |
| `typewriter-test` | 4×12 typewriter matrix |
| `mouse-test` | Sensor clamp, scroll, buttons |
| `memory-test` | SPD XOR + walking/checker/LCG patterns (NR Ch. 7.1) |
| `printer-test` | Motor/paper/head synthetic self-test (ISM #5) |
| `plotter-test` | Workspace + path; midpoints via NR Ch. 3.1 lerp |
| `psu-test` | PSU load steps + telemetry (S8.3; NR Ch. 4.1 Simpson) |
| `ups-test` | UPS load + capacity/SOC (S8.3; NR Ch. 4.1 Simpson) |
| `hid-test` | Soft S8.2 SQHID1 mock USB HID (`mock` / `.sqhid`; live deferred) |
| `demo` | All against fixtures |

## Numerical Recipes

- RAM random patterns: Park–Miller LCG (NR **Ch. 7.1** class)
- Plotter polyline midpoints: `sqn_lerp` (NR **Ch. 3.1**)
- PSU/UPS schedule energy: Soft composite Simpson (NR **Ch. 4.1** class)

Soft original code only — no NR listings.

## Related

Victor warship / bomber Soft checks live in `squirrelshipbench/` (S8.5).
