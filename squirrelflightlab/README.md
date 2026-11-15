# Fighter Jet Flight Lab (SquirrelFlightLab)

**Status: Scaffolded**

First-party SquirrelSoft lab for designing, simulating, and validating the
International Squirrel Machines (ISM) fighter jet — aerodynamics, flight
controls, avionics behaviour, and structural assumptions — without treating
third-party flight sims as the source of truth. Contemporary aero tools may be
studied as reference only; this product is pure SquirrelSoft / Squirrel-OS/2.

Working title alternatives (open): *SquirrelFlightLab*, *ISM Flight Lab*,
*SquirrelAero Lab*.

```
make -C squirrelflightlab            # build/squirrelflightlab
make -C squirrelflightlab test       # unit + CLI smoke + happy-path demo
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
| `fl_project` | Workspace + `.sqflt` design inputs (fuselage, planform stubs, surfaces, mass/CG, actuator map) | Lock scale against ISM hardware docs |
| `fl_aero` | Parametric CL/CD/Cm (+ lateral stubs), AoA sweep, stability derivatives, CSV export | Replaceable higher-fidelity aero model |
| `fl_dyn` | Rigid-body 6-DOF sketch, throttle / pitch / roll / yaw, pause/step/reset, history | Better integration, actuators, atmosphere |
| `fl_avionics` | IMU / radio / actuator channel co-check vs device-doc expect map | Shared types with ISM KiCad bay; HIL |
| `fl_scenario` | `trim_cruise`, `pull_up`, `roll` with editable thresholds | More envelopes + regression suites |
| `fl_planned` | CFD mesh import, wind-tunnel correlation, HIL — **stubbed** | Implement when hardware/CFD land |

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
include/   portable headers (fl_*.h)
src/       portable core + CLI main.c
samples/   ism-fighter-v0.sqflt + device-doc channel stub
scripts/   demo_happy_path.sh
tests/     test_flightlab.c, cli_smoke.sh
docs/      overview.md (architecture + decisions)
```

Original code only. No third-party flight-sim engines.
