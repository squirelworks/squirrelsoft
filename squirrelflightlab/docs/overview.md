# Fighter Jet Flight Lab — design note

**Status: Scaffolded (v0.0.1)**  
**Product line:** SquirrelSoft applications paired with ISM hardware  
**Voice:** playful but engineering-serious; scaffold first; independent designs

## Purpose

Build first-party software so ISM hardware engineers and SquirrelSoft
developers can design, simulate, and validate the ISM fighter jet against the
same device docs. Third-party flight sims are not the source of truth;
published aero references may inform models but must not be vendored.

## Architecture

```
.sqflt project ──► fl_project ──► fl_aero (parametric model)
                       │              │
                       │              ▼
                       ├────────► fl_dyn (6-DOF stub + history)
                       │              │
                       ├────────► fl_avionics (channel co-check + samples)
                       │
                       └────────► fl_scenario (trim / pull-up / roll)

ISM fighter-jet/interfaces/*.yaml
        │
        ▼
fl_yaml ──► fl_iface (schema_version + Soft top-level allowlist)
        │
        └─ CLI: ism-interfaces DIR

fl_planned ── stubs only: CFD mesh, wind tunnel, HIL
main.c     ── thin CLI (future: sqgui window on the same core)
```

Portable C11 core in this app folder (same pattern as SquirrelGNSS): no OS
calls in the physics modules beyond what the CLI needs for files. Dynamics and
aero take structs; the front-end owns I/O. Extension points:

- Swap `fl_aero_from_project` / `FlAeroModel` for a higher-fidelity provider
- Shared channel / pin types with the ISM fighter-jet hardware repo
- HIL transport behind `fl_planned_hil_connect` once the PCB protocol exists
- Optional `libs/sqflight` extraction if another app needs the same core (S6)

## Soft lane rule
Steve / Soft programs **test software only** (simulators, harnesses, benches).
Victor owns fighter-jet hardware/ICD files (ISM #88). Soft never designs the
airframe PCB or shell.

## MVP modules (what v0 actually does)

0. **ISM param/interface load** — `fl_ism` reads Victor's (or CI fixture)
   `.ismfj-params` / `.ismfj-iface`; `fcs-check` writes a pass/fail artifact.
   `fl_iface` loads every YAML under `fighter-jet/interfaces/` (CI snapshot in
   `fixtures/ism-interfaces/`) gated by Soft `schemas/ism-fj-*/` allowlists
   and `schema_version` (Soft #170 / ROADMAP S7.2b). Soft does not fork SoT.

1. **Project workspace** — default ISM fighter inputs; load/save `.sqflt`
   (fuselage length/scale, wing planform placeholders, control surfaces,
   mass/CG stubs, actuator channel map aligned to the KiCad bay stub).
2. **Aerodynamics sandbox** — parametric CL/CD/Cm (+ lateral stubs), AoA
   sweep, basic stability derivatives, ASCII plot + CSV table.
3. **Flight-dynamics sketch** — rigid-body 6-DOF Euler step; throttle and
   pitch/roll/yaw commands; pause/step/reset; short time-history CSV.
4. **Avionics co-check** — expected IMU/radio/actuator roles vs project map;
   flag missing roles and pin mismatches; sample simulated sensor outputs.
5. **Scenario runner** — `trim_cruise`, `pull_up`, `roll` with editable
   pass/fail thresholds on the CLI.
6. **Planned surfaces** — CFD / tunnel / HIL commands exist and refuse with a
   clear “not implemented in scaffold v0” message.

## Units and axis conventions

| Quantity | Unit in project / CLI | Internal |
|----------|----------------------|----------|
| Airframe length, chord, span, CG | mm | m for dynamics |
| Mass | g | kg |
| Inertia stubs | g·mm² | kg·m² (`× 1e-9`) |
| Angles on CLI | degrees | radians |
| Force / moment | — | N, N·m |
| Air density | — | kg/m³ (ISA SL stub 1.225) |

**Body axes:** +X out the nose, +Y toward the right wing, +Z down.  
**Earth (scaffold):** flat NED (+X north, +Y east, +Z down).  
**Controls:** positive elevator trailing-edge down increases CL in the stub
(sign convention recorded here so a later hardware map can flip it).

## Source of truth

| Concern | Authority |
|---------|-----------|
| Fuselage / wing / surface geometry | ISM fighter-jet hardware docs / drawings |
| Mass, CG, inertia | ISM mass properties sheet (when published) |
| Avionics header pins / channel roles | KiCad avionics bay in the ISM hardware repo |
| Parametric aero coefficients | This lab (replaceable); not a third-party sim |
| Pass/fail scenario thresholds | Editable here; engineering judgement |

Until the hardware repo publishes locked numbers, samples use the **~420 mm
fuselage** placeholder called out in the open decisions.

## Open product decisions

1. **Default scale** — keep ~420 mm fuselage as the working default, or wait
   for a locked hardware drawing before any public demo numbers?
2. **Livery / UI accent** — when an `sqgui` window lands, which ISM accent
   colour and panel chrome (Warp-grey vs fighter-lab specific)?
3. **v1 aero fidelity** — freeze the parametric model with documented
   derivative sources, or jump to data-table / simple panel methods first?
4. **Shared lab vs family** — do bombers / rockets later share
   SquirrelFlightLab with vehicle profiles, or get sibling apps?
5. **Naming** — keep “Fighter Jet Flight Lab”, prefer “SquirrelFlightLab”, or
   “ISM Flight Lab” on the packaging label?
6. **Shared types location** — stay in-app until S6, or introduce
   `libs/sqflight` earlier for the hardware repo to compile against?

## OS/2 ground-station host gaps

Squirrel OS/2 is the planned **lab / ground** host (not flight-critical FCC).
Capability gap matrix vs this app:

- [squirrel-os2 `documentation/guides/ism-flightlab-gaps.md`](https://github.com/squirelworks/squirrel-os2/blob/cursor/ism-flightlab-gaps-b55f/documentation/guides/ism-flightlab-gaps.md)
  (branch until merged to `main`)
- Tracker: [squirrel-os2#225](https://github.com/squirelworks/squirrel-os2/issues/225) (lane `-b55f`)

Soft keeps owning Flight Lab / HIL UI code here; OS/2 owns UART/USB CDC,
soft-RT timing, freestanding packaging, and the lab setup guide.

## What to deepen next

1. Correlate parametric derivatives to a published (non-classified) method note
2. Actuator rate limits and surface hinge-moment stubs from the hardware map
3. Wire `sqgui` for live strip charts (same core, freestanding-friendly draw)
4. Import path for tunnel CSV → override `FlAeroModel` coefficients
5. HIL serial framing once the avionics firmware protocol exists
6. Track OS/2 host gaps (#225) as packaging / HIL transport unblocks

## Non-goals (reminders)

No photoreal cockpit, multiplayer, weapons employment, classified/military
data, full CFD solver, certified flight-test claims, or checked-in
Gerbers/STLs (those stay in the ISM hardware repo).
