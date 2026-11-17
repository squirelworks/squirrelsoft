# RAM self-test overview

Eric owns RAM-module hardware in the ISM repo. This Soft app loads Soft-side
ISM-MEM fixtures, checks the Soft SPD XOR contract, runs synthetic bank
patterns (const / walking / checkerboard / Park–Miller LCG — NR Ch. 7.1 class,
Soft original), and writes a pass/fail report artifact for CI and operators.
Soft #203 adds USB-CDC `ISM SPD:` framing (`--spd-cdc`). Soft #204 adds Soft
host Soft-driven Soft live Soft march (`--live-march stub|.sqmem`; ACM Soft
MEM CDC deferred; see `squirrelismbench/docs/psram-live.md`). The bench
harness stays in `squirrelismbench/`; this binary is the companion utility.
