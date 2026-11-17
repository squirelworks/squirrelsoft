# SquirrelISMBench — design note

**Lane:** Soft test software only (Steve). Hardware changes → issues for Eric
(consumer) or Victor (military).

## Purpose

Verify ISM consumer devices against interfaces documented in each device
README. Covers keyboard, mouse, typewriter matrix, RAM-module SPD /
synthetic bank self-tests, printer/plotter, and Soft S8.3 PSU/UPS benches.

## Architecture

```
ISM device README / interfaces  →  ib_iface / ib_memory / ib_printer / ib_plotter / ib_power
        │
        ├─ ib_keyboard  matrix self-test + boot HID report (synthetic)
        ├─ ib_mouse     sensor/scroll/button self-test (synthetic)
        ├─ ib_memory    SPD XOR + walking/checker/LCG patterns (synthetic);
        │               Soft #203 USB-CDC `ISM SPD:` parse + .sqspd stub
        ├─ ib_printer   motor / paper / head SPI stub (synthetic)
        ├─ ib_plotter   workspace + NR Ch.3.1 lerp path check (synthetic)
        ├─ ib_power     PSU/UPS load steps + Soft Simpson energy (NR Ch.4.1)
        └─ ib_hid       Soft SQHID1 mock USB HID framing (S8.2; live hidraw deferred)
main.c  CLI; `memory-test --spd-cdc` (Soft #203); live hidraw / PSRAM (#204) planned
```

## RAM patterns and NR

Synthetic bank tests: all-0 / all-1, walking-1 / walking-0, checkerboard, and
a Park–Miller LCG fill/verify stream. The LCG is the NR **Ch. 7.1** “minimal
standard” class (a=16807, m=2³¹−1), implemented in Soft original code as a
test oracle — **no Numerical Recipes source is copied**.

USB-CDC SPD framing: `docs/spd-cdc.md`. Soft stub
`fixtures/memory/cdc-spd-stub.sqspd`.

SPD Soft contract (fixture): last of N bytes equals XOR of the preceding
bytes; I2C address fixed at `0x50` per ISM `memory/README` Steve interface.

## PSU / UPS energy (S8.3)

`psu-test` / `ups-test` integrate Soft load schedules with a **composite
Simpson** panel per dwell (NR **Ch. 4.1** class). Soft original code — **no
Numerical Recipes listings**. UPS also gates schedule energy ≤ `capacity_wh`.

## Fixtures vs SoT

CI fixtures are temporary Soft copies of a proposed contract. When Eric lands
machine-readable interfaces in ISM, refresh fixtures from that tree and point
operators at the ISM paths. Typewriter fixture tracks ISM #4 / PR #159
(4×12 ROW0–3 × COL0–11). PSU/UPS fixtures are Soft-proposed until SoT lands.
