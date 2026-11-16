# SquirrelHILBench — design note

**Lane:** Soft operator UI only (lane `-2eaf`). Hardware / harness changes →
Victor in `international-squirrel-machines` (ISM #80). Steve’s Flight Lab
(`squirrelflightlab/`) remains the aero / FCS lab — this app does not fork it.

## Purpose

Drive and observe hardware-in-the-loop benches for ISM fighter avionics and
actuation: connect to a bench profile (mock transport in v0), show channel
health, run canned stimulus scripts, and export logs Flight Lab tooling can
recognise by replay CSV naming / header.

## Architecture

```
ISM fighter-jet harness notes / interfaces  →  Soft fixtures (CI) or live path
        │
        ├─ hb_profile   parse + mock connect (estop / safeing display)
        ├─ hb_health    actuators / inceptors / bus channel table
        ├─ hb_stimulus  canned inject script runner
        └─ hb_log       Flight Lab–compatible history CSV (hil-*-replay.csv)
main.c  CLI; live serial/USB planned once ISM freezes the harness protocol
```

## Weapons boundary

Stores integration and **display** only (`stores_display` channels, safeing
interlock display). Soft must not implement guidance, seekers, propulsion,
warheads, fuzing, arming codes, or weapon-protocol internals.

## Fixtures vs SoT

CI fixtures under `fixtures/ism-fighter-hil/` are Soft mocks. Refresh from ISM
`fighter-jet/` when harness / connector docs land. Link from the app README.
