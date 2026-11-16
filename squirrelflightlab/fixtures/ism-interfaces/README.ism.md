# ISM-FJ-1 Machine-Readable Interfaces

Source of truth for **SquirrelSoft Flight Lab** (Steve) and other consumers. Human narratives remain under sibling dirs; these files are the loadable contracts.

| File | Contents | Status |
|------|----------|--------|
| `schema-manifest.yaml` | Index of interface files, versions, drawing cross-refs | Rev A |
| `aircraft-geometry.yaml` | Datum, study scale, major dimensions, stations | Stub with numbers |
| `mass-properties.yaml` | Mass, CG, inertia tensor placeholders | Stub schema + provisional numbers |
| `flight-controls.yaml` | Surfaces, rates, law modes, envelope limits | Stub with design defaults |
| `sensors.yaml` | Nav/air-data/radar-EO I/O limits & rates | Stub |
| `hardpoints.yaml` | Stores stations, mass classes, safeing flags | Stub aligned to DWG-040 |
| `actuators.yaml` | EHA channels, authority, failure behaviour | Stub |
| `avionics-bus.yaml` | Bus names, frame rates, topic list | Stub |
| `materials-zones.yaml` | Airframe zone → material map + joints | Stub (#92) |
| `carrier-loads.yaml` | Arrestment / catapult / sink-rate / wing-fold cases | Stub (#93) |
| `inert-stores.yaml` | Inert store mass/dims/drag/CG (public class figures) | Stub (#94) |
| `sms-interface.yaml` | SMS topics, safeing, separation envelopes | Stub (#94) |
| `manufacturing.yaml` | Zone process/spec/NDT/supply (research) | Stub (#100) |
| `multi-origin-stores.yaml` | Family×station×adapter + inert catalogue | Stub (#101) |
| `viability-criteria.yaml` | Sanity gates + class bands (canonical) | Stub (#103) |
| `materials-allowables.yaml` | Spec callouts, allowables, stack-ups, joints | Stub (#71) |

## Rules
1. Units are SI unless a field ends in `_mm` / `_deg` / `_pct` (study-model companions).
2. `status: stub` means Flight Lab may load it; numbers marked `provisional: true` will change when product decisions freeze.
3. Drawing IDs (`ISM-FJ-DWG-…`) in `source_drawings` must match `diagrams/` index.
4. Do not fork these into SquirrelSoft — import from this tree.

## Consumer map
| Consumer | Primary files |
|----------|---------------|
| Flight Lab (#79 / SS#168) | geometry, mass, flight-controls, hardpoints, sensors, inert-stores, sms, carrier-loads, materials-zones |
| HIL bench (#80) | actuators, avionics-bus, sensors |
| Mission-computer stubs (#81) | avionics-bus, sensors, hardpoints |
| HUD/HMI (#82) | flight-controls (modes), sensors |
| Data-bus analyzer (#83) | avionics-bus |
