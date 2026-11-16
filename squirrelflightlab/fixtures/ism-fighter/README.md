# ISM-FJ CI fixtures (temporary Soft snapshots)

**Source of truth:** `github.com/squirelworks/international-squirrel-machines`
`fighter-jet/` (Victor). Soft does not design the airframe.

These files mirror numbers already published in ISM docs / OpenSCAD so Flight
Lab CI can load an ISM parameter set before Victor lands machine-readable
files (tracked: ISM #88).

| Soft fixture | ISM origin |
|--------------|------------|
| `ism-fj-1.ismfj-params` | `shell/airframe.scad`, `diagrams/stations.md`, control-surface notes |
| `bay-io.ismfj-iface` | `diagrams/wiring-harness.md`, `kicad/README.md` (J1–J4 → A1–A4) |

When ISM #88 lands, prefer loading from a checkout of
`fighter-jet/parameters/` and `fighter-jet/interfaces/`; keep fixtures only as
offline CI copies refreshed from ISM.
