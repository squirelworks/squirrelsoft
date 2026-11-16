# SquirrelShipBench

**Status: Scaffolded (Soft test equipment)**

Soft-side harness for Victor’s **warship** scale-model control board / teaching
hydrostatics (ISM #43) and **hypersonic bomber** educational heat-flux stub
(ISM #37). Soft does not design hulls or airframes.

```
make -C squirrelshipbench
make -C squirrelshipbench test
./build/squirrelshipbench demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `hydro-test` | Soft GM/BM oracle + Simpson I_T (NR Ch. 4.1 class) |
| `board-test` | Scale-model channel allowlist; weapons roles fail |
| `heatflux-test` | Teaching cold-wall q ≈ k√(ρ/Rn) V³ |
| `demo` | All fixtures |

## Numerical Recipes

Warship waterplane transverse inertia uses Soft original **Simpson’s rule**
(NR **Ch. 4.1** class of equally spaced Newton–Cotes formulas). No NR listings
are copied.

## Weapons boundary

No weapons interfaces, fire control, seekers, guidance, warheads, or fuzing.
Mass placeholders stay on the ISM side.
