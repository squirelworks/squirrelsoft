# Flight Lab credibility summary (NASA-STD-7009 style, Soft #172)

**Label on every output:** provisional, unvalidated against tunnel/flight test.

| Element | Verification | Validation | Notes |
|---------|--------------|------------|-------|
| Integrator (RK4 / DP5(4)) | Energy drift cases in `libs/squirrelnum` + `fl_verify` | — | NR Ch. 17 cited; no listing ports |
| Units / scale | mm/g → SI helpers + mix-up tests | — | `docs/units-and-scale.md` |
| Pendulum / torque-free / ballistic | Closed-form oracles in `fl_verify` | — | Soft #172 |
| Steady trim | Brent residual on scaffold aero | — | Soft aero, not tunnel |
| Phugoid closed form | Textbook T_ph = 2πV/(g√2) | — | Etkin/Nelson-style approx |
| Short-period | Soft response presence check | Deferred | Full eigenvalue compare later |
| ISM-FJ viability gates | Soft recompute vs class bands (`fl_viability`) | Soft #176 | MD+JSON; provisional label |
| G-BREGUET Soft ferry | Soft Breguet + Brent L/D oracle (NR Ch. 9.3) | Soft #176 follow-up | Raymer form; squirrelnum Brent |
| G-CG Soft travel | Soft %MAC from FS span | Soft #176 follow-up | vs `cg_travel_pct_mac` band |
| G-STRUCT-MARGIN Soft | Soft ultimate = limit×factor ≥ nz×factor | Soft #176 follow-up | Textbook ultimate factor |
| ISM-FJ aero vs AVL/OpenVSP/CalculiX | Optional path presence | Soft follow-up | Skip if artifacts absent; full table compare later |
| F-16 / NESC cases | Not in this PR | Soft follow-up | Licence check before vendoring CSV |
| Tunnel CSV aero provider (S7.3) | Soft lerp + correlate vs parametric | Soft synthetic fixture | NR Ch. 3.1 / 15.1 class; real tunnel logs later |
| Strip chart (S7.4) | Headless sqgui draw + PPM; segment count | Soft sim history | NR Ch. 3.1 resample; SDL window deferred |
| Eric scale / RC correlation | — | Soft follow-up | Needs Eric logging formats |

ISM SoT snapshot: `fixtures/ism-interfaces/` (refresh from ISM `main`). Soft does not design hardware.
