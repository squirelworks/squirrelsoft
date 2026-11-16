# libs/squirrelnum

Portable C11 numerical methods for Soft Flight Lab (and later Soft consumers).

**Primary reference:** *Numerical Recipes* 3rd ed. (Cambridge) for algorithm
choice, step-size control, error behaviour and pitfalls. **Do not copy NR
code listings** — their licence forbids redistribution. Each routine is
original Soft code; doc comments cite the NR chapter/section and the
underlying papers (Dormand–Prince, Brent, etc.).

## Contents

| API | Role | Method cites |
|-----|------|----------------|
| `sqn_rk4_*` | Fixed-step RK4 | NR Ch. 17.1; Kutta 1901 |
| `sqn_rk45_*` | Adaptive DP5(4) | NR Ch. 17.2; Dormand & Prince 1980 |
| `sqn_brent` | Root finding | NR Ch. 9.3; Brent 1973 |
| `sqn_lerp` / `sqn_spline_*` / `sqn_table_lookup` | Interpolation | NR Ch. 3.1 / 3.3; de Boor |
| `sqn_gesv` | Small dense solve | NR Ch. 2.1–2.3 |
| `sqn_quat_*` | Attitude quaternions | Shoemake 1985; Hamilton |

```bash
make -C libs/squirrelnum test
```

Flight Lab links this library for dynamics integration (Soft #174).
