# SquirrelBusMon

**Status: Scaffolded** (S10.4 / [ISM #83](https://github.com/squirelworks/international-squirrel-machines/issues/83))

Soft-side **avionics data-bus analyzer** for International Squirrel Machines
fighter traffic: import a sample capture, decode against an ICD catalog
fixture, filter by node / message ID / topic / bus, and emit a replay stream
for Flight Lab or HIL.

Protocol SoT is ISM — Soft holds sample fixtures only:

- [avionics-bus.yaml](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/avionics-bus.yaml)
- [sms-interface.yaml](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/sms-interface.yaml)

Weapons boundary: SMS/stores topics are **opaque** (topic name + safe display
fields). No weapon-protocol payload decoding.

```
make -C squirrelbusmon
make -C squirrelbusmon test
./build/squirrelbusmon demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `decode [--catalog FILE] [--capture FILE]` | Decode capture vs ICD catalog |
| `filter … [--node\|--msgid\|--topic\|--bus]` | Same decode, then filter |
| `replay … [--out FILE]` | Emit REPLAY v1 for Flight Lab / HIL |
| `demo` | Decode + filter + replay against fixtures |

## Replay hook

Documented in [`docs/replay-hook.md`](docs/replay-hook.md).

## Related

- ISM #83 — this analyzer
- Soft Flight Lab: `squirrelflightlab/` (Steve; separate from S10 fighter UI)
- Soft S10 siblings: HIL bench UI (#80), MC stubs (#81), HUD tool (#82)
