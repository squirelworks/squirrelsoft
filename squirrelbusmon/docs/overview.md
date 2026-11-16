# SquirrelBusMon — design note

**Lane:** Soft fighter UI (Victor / ISM #83). Not Flight Lab (`squirrelflightlab/`).

## Purpose

Capture (file import), decode, filter, and replay ISM avionics bus frames for
bring-up and co-check. Soft owns the analyzer app and sample fixtures. Protocol
source of truth stays in ISM:

- [avionics-bus.yaml](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/avionics-bus.yaml)
- [sms-interface.yaml](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/sms-interface.yaml)

## Architecture

```
ISM avionics-bus.yaml (SoT)     Soft fixtures/catalog/*.ismbus
        │                              │
        └──────── topic names ─────────┘
                       │
              bm_catalog + bm_frame decode
                       │
         ┌─────────────┼─────────────┐
         ▼             ▼             ▼
      decode        filter        replay (REPLAY v1)
                                      │
                         Flight Lab / HIL ingest
                         (docs/replay-hook.md)
```

## Weapons boundary

Topics marked `opaque=1` in the Soft catalog (SMS / stores) expose only the
topic name plus safe display fields (`mode`, `state`, `stations=<opaque>`,
`inhibit`, `reason`, `station_id`). No guidance, seekers, propulsion, warheads,
fuzing, arming codes, or weapon-protocol payload decoding.

## Formats

Original Soft text formats (`ISM-BUS-ICD 1`, `ISM-BUS-CAPTURE 1`) for CI. When
Victor publishes machine-readable analyzer fixtures in ISM, refresh Soft
samples and keep linking the ISM protocol docs.
