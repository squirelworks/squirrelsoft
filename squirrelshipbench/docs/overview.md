# SquirrelShipBench — design note

**Lane:** Soft test software (Steve). Hardware / program design → Victor (ISM).

## Purpose

CI-friendly Soft checks against Soft copies of ISM warship and hypersonic-bomber
interfaces until machine-readable SoT is wired live.

## Hydrostatics

`GM = KB + BM − KG` with `BM = I_T / ∇`. Soft recomputes `I_T` from waterplane
half-breadth ordinates via composite Simpson 1/3 (NR Ch. 4.1 class) and
compares to the rectangular teaching reference (`L B³ / 12`).

## Control board

Only scale-model actuator / telemetry channel roles are allowed. Soft rejects
roles containing weapons / fire-control / seeker / guidance / warhead / fuzing /
arming substrings.

## Bomber heat flux

Educational Sutton–Graves–style identity from ISM teaching stubs. Soft verifies
monotonicity in nose radius. Not a TPS product or trajectory code.
