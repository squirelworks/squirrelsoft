# SquirrelMCStubs — design note

**Lane:** Soft test software only (Steve). Airframe / operational flight
software → Victor / ISM. **Stubs are not flight software.**

## Purpose

Provide headless mission-computer stub processes so Flight Lab and CI can
co-check bus topic names and display-only stores/safeing state without
operational flight software (ISM #81).

## Roles

```
fixtures/message-catalog.yaml  →  mc_catalog  (topic names)
fixtures/safeing-display.yaml  →  mc_sms      (loadout flags + safeing display)
        │
        ├─ mc_stub      MC: synthetic mc.stores_config / radar_tracks / mode_request
        ├─ mc_sms       SMC/SMS: station_loadout_flags + safeing display ONLY
        └─ mc_display   one display-page publisher (stores_safeing page)
main.c  CLI: catalog | mc-tick | sms-tick | display-tick | demo
```

## Weapons boundary

SMC/SMS stub code paths may read and print:

- `station_loadout_flags` (`empty` / `hung` / `selected`)
- safeing display flags (`mechanical_ground_pins`, `wow_inhibits_release`,
  `dual_consent`, `fcc_envelope_veto`, `jettison_guarded`, `safe_arm_discrete`)

They must not implement or emit guidance, seekers, propulsion, warheads,
fuzing, arming codes, or weapon-protocol internals. Catalog fixtures may list
ICD topic **names** (including `mc.stores_release_cmd`) for ID matching only.

## Fixtures vs SoT

CI fixtures are Soft copies of proposed ICD topic names. When Victor freezes
machine-readable interfaces in ISM, refresh from
`squirrelflightlab/fixtures/ism-interfaces/` (or the ISM tree) and keep Soft
as a consumer, not a fork of SoT.
