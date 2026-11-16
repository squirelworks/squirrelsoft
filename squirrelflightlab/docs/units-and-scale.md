# Flight Lab units and scale

**Status:** Soft test software (Steve). ISM publishes study-model, RC and
full-scale numbers; Soft must not mix them silently.

## Rule

| Layer | Units | Notes |
|-------|-------|-------|
| Project / ISM study inputs | **mm**, **g**, **g·mm²** | Matches ISM study model (~420 mm fuselage) |
| Dynamics / aero internals | **SI** (m, kg, kg·m², N, rad) | All integration in SI |
| CLI angles | degrees in / out | Converted at the boundary |

Conversions are explicit helpers in `fl_units.h`:

- `fl_mm_to_m` / `fl_m_to_mm`
- `fl_g_to_kg` / `fl_kg_to_g`
- `fl_g_mm2_to_kg_m2` (× 1e-9) — **never** treat g·mm² as kg·m²

## Scale modes (study / RC / full-scale)

ISM also publishes full-scale (~15.5 m, ~9800 kg) and RC targets. Soft labels
the active scale on the project (`fl_scale_mode`) and converts lengths/masses
through named helpers. Froude scaling for time/frequency is documented for
later correlation with Eric’s RC data (NASA TP-1435); verification suite
(#172) expands the Froude check.

## Catching mm/g mix-ups

CI tests assert:

1. Converting 420 mm → 0.42 m (not 420 m).
2. Converting 500 g → 0.5 kg (not 500 kg).
3. Inertia: 1 g·mm² → 1e-9 kg·m².
4. Using raw `mass_g` as kg in a mock RHS would fail a golden energy check
   (mass off by 1000×).

Every accuracy report must stay labelled
**provisional, unvalidated against tunnel/flight test**.
