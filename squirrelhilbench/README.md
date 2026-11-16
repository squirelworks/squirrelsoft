# SquirrelHILBench

**Status: Scaffolded** (ISM [#80](https://github.com/squirelworks/international-squirrel-machines/issues/80))

Soft-side **fighter HIL bench UI** for International Squirrel Machines (Victor).
Operator commands: load a mock bench profile, show actuator / inceptor / bus
channel health, run a canned stimulus script, and save a log whose CSV header
and default `*-replay.csv` naming match Fighter Jet Flight Lab time-history
exports.

**Physical bench / harness SoT:**
[`squirelworks/international-squirrel-machines`](https://github.com/squirelworks/international-squirrel-machines)
`fighter-jet/` (interfaces for actuators, avionics-bus, sensors). Soft owns UI
only; see `fixtures/ism-fighter-hil/README.md`.

Steve’s Flight Lab lives in `squirrelflightlab/` (ISM #79) — do not duplicate
aero / dynamics work here.

```
make -C squirrelhilbench
make -C squirrelhilbench test
./build/squirrelhilbench demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `load [--profile FILE]` | Parse mock bench profile and connect |
| `health [--profile FILE]` | Channel health table + estop / safeing |
| `stimulus [--profile FILE] [--script FILE] [--csv OUT]` | Run canned stimulus; write Flight Lab replay CSV |
| `demo` | All of the above against CI fixtures → `build/hil-stim-replay.csv` |

## Weapons boundary

Stores integration and display only. No guidance, seekers, propulsion,
warheads, fuzing, arming codes, or weapon-protocol internals.

## Planned (not in v0)

- Live serial / USB transport against the real avionics PCB harness
- `sqgui` strip charts on the same core
- Preset packs refreshed from published ISM harness notes
