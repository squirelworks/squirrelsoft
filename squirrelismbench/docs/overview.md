# SquirrelISMBench — design note

**Lane:** Soft test software only (Steve). Hardware changes → issues for Eric
(consumer) or Victor (military).

## Purpose

Verify ISM consumer devices against interfaces documented in each device
README. Covers keyboard, mouse, typewriter matrix, and RAM-module SPD /
synthetic bank self-tests.

## Architecture

```
ISM device README / interfaces  →  ib_iface / ib_memory / ib_printer / ib_plotter
        │
        ├─ ib_keyboard  matrix self-test + boot HID report (synthetic)
        ├─ ib_mouse     sensor/scroll/button self-test (synthetic)
        ├─ ib_memory    SPD XOR + walking/checker/LCG patterns (synthetic)
        ├─ ib_printer   motor / paper / head SPI stub (synthetic)
        └─ ib_plotter   workspace + NR Ch.3.1 lerp path check (synthetic)
main.c  CLI; live USB HID / I2C planned
```

## RAM patterns and NR

Synthetic bank tests: all-0 / all-1, walking-1 / walking-0, checkerboard, and
a Park–Miller LCG fill/verify stream. The LCG is the NR **Ch. 7.1** “minimal
standard” class (a=16807, m=2³¹−1), implemented in Soft original code as a
test oracle — **no Numerical Recipes source is copied**.

SPD Soft contract (fixture): last of N bytes equals XOR of the preceding
bytes; I2C address fixed at `0x50` per ISM `memory/README` Steve interface.

## Fixtures vs SoT

CI fixtures are temporary Soft copies of a proposed contract. When Eric lands
machine-readable interfaces in ISM, refresh fixtures from that tree and point
operators at the ISM paths. Typewriter fixture tracks ISM #4 / PR #159
(4×12 ROW0–3 × COL0–11).
