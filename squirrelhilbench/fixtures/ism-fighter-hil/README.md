# ISM fighter HIL fixtures (Soft CI)

Temporary Soft copies of a **mock** HIL bench profile and stimulus script for
CI. Physical harness definitions, connector maps, and fixture notes are SoT in
[`squirelworks/international-squirrel-machines`](https://github.com/squirelworks/international-squirrel-machines)
under `fighter-jet/` (see interfaces consumed by HIL: actuators, avionics-bus,
sensors — ISM issue [#80](https://github.com/squirelworks/international-squirrel-machines/issues/80)).

| Soft file | Role |
|-----------|------|
| `bench-profile.ismhil` | Mock transport + channel health map |
| `stimulus-trim.stim` | Canned inject script (`trim_cruise`) |

When Victor publishes harness notes in ISM, refresh these fixtures from that
tree and point operators at the ISM paths. Soft owns UI code only.
