# SquirrelMCStubs

**Status: Scaffolded**

Soft-side **mission-computer stubs** for the ISM fighter ICD (bus nodes and
display pages). Flight Lab / HIL can co-check against these processes without
full operational flight software.

**These stubs are not flight software.** They publish deterministic synthetic
topics and display fields for CI and lab co-check only. They are a separate Soft
app from Fighter Jet Flight Lab (`squirrelflightlab/`) — do not duplicate that
aero / FCS lab here.

Weapons boundary (SMS/stores): **display + `station_loadout_flags` + safeing
display fields only**. No guidance, seekers, propulsion, warheads, fuzing,
arming codes, or weapon-protocol internals. `live_fuzing_in_repo` must stay
`false` (parse rejects `true`).

```
make -C squirrelmcstubs
make -C squirrelmcstubs test
./build/squirrelmcstubs demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `catalog [--catalog FILE]` | Parse fixture topic catalog |
| `tick [--catalog FILE] [--safeing FILE] [--n N]` | Deterministic MC + SMS + display ticks |
| `mc-tick [--ticks N]` | Deterministic MC stub publish ticks |
| `sms-tick [--ticks N]` | SMC/SMS display-only safeing/loadout ticks |
| `display-tick [--ticks N]` | One stores/safeing display-page publisher |
| `demo` | All roles against CI fixtures |

## Fixtures vs SoT

Topic names under `fixtures/message-catalog.yaml` mirror Soft’s copy of ISM
`fighter-jet/interfaces/` (avionics-bus + sms-interface). Refresh pattern:
copy names from `squirrelflightlab/fixtures/ism-interfaces/` when ISM drafts
move. ICD ownership stays in `squirelworks/international-squirrel-machines`.

## Related

- ISM #81 — mission-computer stubs for fighter ICD
- Flight Lab: `squirrelflightlab/` (aero / FCS / interface load — not stubs)
- ISM device bench (consumer): `squirrelismbench/`
