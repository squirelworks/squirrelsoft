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
| ISM-FJ aero vs AVL/OpenVSP | Optional file harness | Soft #176 | Skip if artifacts absent |
| F-16 / NESC cases | Not in this PR | Soft follow-up | Licence check before vendoring CSV |

ISM SoT snapshot: `fixtures/ism-interfaces/` (refresh from ISM `main`). Soft does not design hardware.
