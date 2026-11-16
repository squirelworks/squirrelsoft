# SquirrelBusMon fixtures

Soft-held **sample** ICD catalog and bus capture for CI. Protocol source of
truth remains in International Squirrel Machines:

- [`fighter-jet/interfaces/avionics-bus.yaml`](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/avionics-bus.yaml)
- [`fighter-jet/interfaces/sms-interface.yaml`](https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/interfaces/sms-interface.yaml)
  (stores/SMS topics — Soft treats as opaque display fields)

Refresh Soft fixtures when Victor lands analyzer fixtures under ISM
`interfaces/` or `fixtures/` (see `expected_behaviour.analyzer_fixtures` in the
avionics-bus YAML). Do not fork protocol docs into Soft.
