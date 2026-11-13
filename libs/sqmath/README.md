# libs/sqmath

The expression engine and first CAS layer for SquirrelCalc (roadmap S5.1).
It is written for Squirrel OS/2 and built here first. It is C11 with no OS
calls and no allocation: an `SqmExpr` (about 13 KB) and an `SqmProgram` (about
7 KB) are plain values the caller owns.

```c
#include "sqmath.h"

SqmExpr f, df;
SqmError err;
if (sqm_parse("x^2 sin(x)", &f, &err) != SQM_OK)
    printf("%s at %d\n", err.msg, err.pos);

double v;
const char *names[] = {"x"};
sqm_eval(&f, names, (double[]){1.5}, 1, &v);     /* 2.2443... */

sqm_diff(&f, "x", &df);                           /* 2*x*sin(x) + x^2*cos(x) */
char text[256];
sqm_print(&df, text, sizeof text);

SqmProgram p;                                     /* fast repeated evaluation */
sqm_compile(&f, names, 1, &p);
double y = sqm_run(&p, (double[]){0.25});
```

## What it does

- **Parsing** calculator input. It handles `+ - * / ^` (also `**`), right-to-left powers, unary minus below
  powers (`-x^2` = `-(x^2)`), factorial `!`, implied multiplication (`2x`,
  `3(x+1)`, `x y`), numbers like `1.5e-3`, the constants `pi` and `e`, and the
  functions sin cos tan asin acos atan sinh cosh tanh exp ln log sqrt cbrt abs
  floor ceil round sign, plus atan2 hypot min max mod with two arguments.
  Errors carry a code, a byte position and a short message.
- **Evaluation**, either by walking the tree or from a compiled stack program (`sqm_compile` and
  `sqm_run`) for graphs and tables.
- **Derivatives** (`sqm_diff`) for every function except min, max, mod and factorial.
  The result comes out simplified: the tree is rebuilt through constructors
  that fold numbers and apply identities. Those include `x + 0`, `1*x`,
  `x - x`, `x*x = x^2`, `x^3/x = x^2`, like terms (`2x + 3x = 5x`), reduced
  fractions (`6/4 = 3/2`), `ln(e) = 1`, and `ln(exp(u)) = u`. Numbers only fold
  where the result stays exact: `sqrt(4)` becomes 2 but `sqrt(2)` stays.
- **Printing** back to text with few parentheses (`sqm_print`). The text parses
  back to the same value.
- **Numerics**: a root in a sign-changing bracket (`sqm_solve`, Illinois false position
  that bisects every fourth step), all roots on an interval (`sqm_roots`, which
  skips poles such as tan at pi/2), and integrals by adaptive Simpson
  (`sqm_integrate`).
- **Calculator display** of a number (`sqm_format`): N significant digits, no
  trailing zeros, `undefined` for NaN.

## Command line

`make` also builds `build/sqcalc`:

```
build/sqcalc "2x^2 - 1" x=3                 17
build/sqcalc --diff x "ln(x)/x"             (1 - ln(x))/x^2
build/sqcalc --simplify "x + 1 + 2x"        3*x + 1
build/sqcalc --solve x -5 5 "x^2 - 2"       x = -1.41421356237 / x = 1.41421356237
build/sqcalc --integrate x 0 1 "4/(1+x^2)"  3.14159265359
build/sqcalc --table x 0 1 0.5 "x^2"
```

## Tests

`make test` runs `tests/test_sqmath.c` and `tests/sqcalc_smoke.sh`. The C tests
cover evaluation, parse errors with their positions, the depth and size
limits, compile-versus-eval agreement, print round trips, derivatives (checked
against central differences), simplification, roots, integrals and the number
display. They also include a seeded random test: 20,000 token strings, where
everything that parses must print, reparse, compile and simplify to the same
value. The suite also passes under `-fsanitize=address,undefined`.

## Next

Polynomial collection and expansion, solving equations symbolically, units, and the SquirrelCalc front-end on `libs/sqgui`.
