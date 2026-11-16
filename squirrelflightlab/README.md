# Fighter Jet Flight Lab (SquirrelFlightLab)

**Status: Scaffolded** (S7.1–S7.2f)

First-party SquirrelSoft lab for simulating and validating the International
Squirrel Machines (ISM) fighter jet — aerodynamics, flight controls, avionics
behaviour — without treating third-party flight sims as the source of truth.
**Airframe / ICD truth is Victor’s ISM repo** (`fighter-jet/`). Soft only loads
and co-checks; Soft does not design hardware.

Working title alternatives (open): *SquirrelFlightLab*, *ISM Flight Lab*,
*SquirrelAero Lab*.

```
make -C squirrelflightlab            # build/squirrelflightlab
make -C squirrelflightlab test       # unit + CLI smoke + happy-path + verify + viability
make -C squirrelflightlab verify     # analytic verification report (Soft #172)
make -C squirrelflightlab viability  # viability-criteria.md/json (Soft #176)
make -C squirrelflightlab demo       # AoA sweep + flight history only
```

## Happy path

```bash
./build/squirrelflightlab demo
# or the scripted path used in CI:
sh scripts/demo_happy_path.sh build
```

That loads the sample ISM fighter project, runs an angle-of-attack sweep
(ASCII CL plot + CSV), records a short 6-DOF time history, and exercises the
trim / pull-up / roll scenarios.

## What the scaffold covers

| Module | Role | Deepen next |
|--------|------|-------------|
| `fl_project` | Workspace + `.sqflt` design inputs | Prefer ISM param files |
| `fl_ism` | Load `ISM-FJ parameters` / `ISM-FJ interface` texts | Refresh fixtures from ISM #88 |
| `fl_iface` | Load all `fighter-jet/interfaces/*.yaml` by `schema_version` + nested Soft allowlists (#177) | Deeper Soft-cared nested paths as Victor evolves stubs |
| `fl_verify` | Analytic verification (pendulum/rigid/ballistic/trim/phugoid) | NESC / F-16 TP-1538 |
| `fl_viability` | Evaluate `viability-criteria.yaml` → MD/JSON | Full AVL/OpenVSP/CalculiX compare |
| `fl_loadout` | Station×adapter×store validation (inert) | More multi-origin matrix cases |
| `fl_fcs` | Scripted FCS co-check + pass/fail report | Envelope numbers from Victor |
| `fl_aero` | Parametric CL/CD/Cm, AoA sweep, CSV | Higher-fidelity provider |
| `fl_dyn` | 6-DOF sketch, history (RK4 via squirrelnum) | Atmosphere + quaternions |
| `fl_avionics` | Channel co-check vs bay expect map | HIL |
| `fl_scenario` | `trim_cruise`, `pull_up`, `roll` | More envelopes |
| `fl_planned` / tunnel | CFD/HIL stubbed; **tunnel CSV provider active (S7.3)** | Real tunnel logs / HIL PCB |
| `fl_chart` | **sqgui strip chart (S7.4)** headless + PPM | Optional SDL live window |

Out of scope for v0: photoreal visuals, multiplayer, weapons, classified data,
full CFD, certified flight-test tooling, shipping Gerbers/STLs.

## Units and axes

Documented in [`docs/overview.md`](docs/overview.md):

- Geometry / mass stubs: millimetres and grams (ISM hardware scale)
- Dynamics: SI (m, kg, N, rad internally; CLI angles in degrees)
- Body axes: +X nose, +Y right wing, +Z down; flat NED for earth position
- **Source of truth** for airframe lengths, mass, and pin maps: ISM fighter-jet
  hardware docs / KiCad avionics bay (external). Values here are placeholders.

## Open decisions

Listed in the design note. Highlights:

1. Default fuselage scale (~420 mm?) vs locking to a published hardware drawing
2. Livery / UI accent when a window lands
3. Which aero fidelity to freeze for v1 (parametric vs panel / data tables)
4. Whether bombers / rockets later share this lab or fork a family of labs

## Code layout

```
include/    portable headers (fl_*.h)
src/        portable core + CLI main.c
samples/    ism-fighter-v0.sqflt + device-doc channel stub
fixtures/   ism-fighter/ (params) + ism-interfaces/ (YAML snapshot)
schemas/    Soft-owned ISM-FJ interface allowlists (not SoT)
scripts/    demo_happy_path.sh
tests/      test_flightlab.c, cli_smoke.sh
docs/       overview.md (architecture + decisions)
```

Original code only. No third-party flight-sim engines.
