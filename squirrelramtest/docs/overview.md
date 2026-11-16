# RAM self-test overview

Eric owns RAM-module hardware in the ISM repo. This Soft app loads Soft-side
ISM-MEM fixtures, checks the Soft SPD XOR contract, runs synthetic bank
patterns (const / walking / checkerboard / Park–Miller LCG — NR Ch. 7.1 class,
Soft original), and writes a pass/fail report artifact for CI and operators.
Live USB-CDC `ISM SPD:` lines and PSRAM march tests remain S8.4b. The bench
harness stays in `squirrelismbench/`; this binary is the companion utility.
