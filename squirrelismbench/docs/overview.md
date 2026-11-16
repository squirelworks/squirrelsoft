# SquirrelISMBench — design note

**Lane:** Soft test software only (Steve). Hardware changes → issues for Eric
(consumer) or Victor (military).

## Purpose

Verify ISM consumer devices against interfaces documented in each device
README. Start with the 60% keyboard and mouse Eric is building now.

## Architecture

```
ISM device README / interfaces/*.ismhid  →  ib_iface parse
        │
        ├─ ib_keyboard  matrix self-test + boot HID report (synthetic)
        └─ ib_mouse     sensor/scroll/button self-test (synthetic)
main.c  CLI; live USB HID planned
```

## Fixtures vs SoT

CI fixtures are temporary Soft copies of a proposed contract. When Eric lands
machine-readable interfaces in ISM, refresh fixtures from that tree and point
operators at the ISM paths.
