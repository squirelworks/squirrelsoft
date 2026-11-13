/*
 * sqmath: expression engine and CAS core for SquirrelCalc (original code).
 *
 * Parses calculator input ("2x^2 - sin(x)/3", "atan2(y, x)", "5!") into an
 * expression tree, evaluates it, compiles it to a small stack program for
 * fast repeated evaluation (graphing, tables), differentiates and simplifies
 * it symbolically, expands and solves equations exactly, prints it back, and
 * finds roots and integrals numerically.
 *
 * C11, no OS calls, no allocation: every structure is a fixed-size value the
 * caller owns (SqmExpr is about 13 KB, SqmProgram about 7 KB).
 */
#ifndef SQMATH_H
#define SQMATH_H

#include <stddef.h>
#include <stdint.h>

#define SQM_MAX_NODES 512 /* nodes in one expression */
#define SQM_MAX_NAMES 32  /* distinct variable names in one expression */
#define SQM_NAME_LEN 16   /* longest name + 1 */
#define SQM_MAX_DEPTH 64  /* nesting of parentheses and operators */
#define SQM_MAX_OPS 512   /* instructions in a compiled program */

enum {
    SQM_OK = 0,
    SQM_ERR_SYNTAX = -1,  /* not a well-formed expression */
    SQM_ERR_FULL = -2,    /* too many nodes, names or instructions */
    SQM_ERR_UNKNOWN = -3, /* a variable with no value, or a name that is not a function */
    SQM_ERR_ARGS = -4,    /* a function called with the wrong number of arguments */
    SQM_ERR_DEPTH = -5,   /* nested deeper than SQM_MAX_DEPTH */
    SQM_ERR_DIFF = -6,    /* no derivative rule (min, max, mod, factorial) */
    SQM_ERR_NAME = -7,    /* name too long */
    SQM_ERR_RANGE = -8,   /* numbers too big for exact fractions */
    SQM_ERR_SOLVE = -9,   /* no exact method for this equation */
    SQM_ERR_NOFORM = -10, /* no exact form found (factor, apart, integral) */
    SQM_ERR_UNITS = -11   /* units that don't fit: adding m to s, converting m to kg */
};

typedef enum {
    SQM_NUM,   /* v */
    SQM_CONST, /* a: 0 = pi, 1 = e; v holds the value */
    SQM_VAR,   /* a: index into names */
    SQM_NEG,   /* -a */
    SQM_ADD,   /* a + b */
    SQM_SUB,   /* a - b */
    SQM_MUL,   /* a * b */
    SQM_DIV,   /* a / b */
    SQM_POW,   /* a ^ b */
    SQM_FUN    /* fn(a) or fn(a, b) */
} SqmKind;

typedef enum {
    SQM_FN_SIN, SQM_FN_COS, SQM_FN_TAN, SQM_FN_ASIN, SQM_FN_ACOS, SQM_FN_ATAN,
    SQM_FN_SINH, SQM_FN_COSH, SQM_FN_TANH, SQM_FN_EXP, SQM_FN_LN, SQM_FN_LOG,
    SQM_FN_SQRT, SQM_FN_CBRT, SQM_FN_ABS, SQM_FN_FLOOR, SQM_FN_CEIL, SQM_FN_ROUND,
    SQM_FN_SIGN, SQM_FN_FACT,
    /* two arguments */
    SQM_FN_ATAN2, SQM_FN_HYPOT, SQM_FN_MIN, SQM_FN_MAX, SQM_FN_MOD,
    SQM_FN_COUNT
} SqmFn;

typedef struct {
    uint8_t kind; /* SqmKind */
    uint8_t fn;   /* SqmFn for SQM_FUN */
    int16_t a, b; /* children (node indices), or the name / constant index */
    double v;     /* SQM_NUM and SQM_CONST */
} SqmNode;

typedef struct {
    int n;     /* nodes used */
    int root;  /* index of the top node, -1 when empty */
    int nnames;
    char names[SQM_MAX_NAMES][SQM_NAME_LEN];
    SqmNode node[SQM_MAX_NODES];
} SqmExpr;

/* Where parsing stopped and why. */
typedef struct {
    int code;    /* SQM_OK or SQM_ERR_* */
    int pos;     /* byte offset into the text */
    char msg[64];
} SqmError;

/*
 * Parse a NUL-terminated expression. Grammar, loosest first:
 *   + -            left to right
 *   * /  and implied multiplication before a name or "(": 2x, 3(x+1), x y
 *   unary - +      so -x^2 is -(x^2)
 *   ^ (or **)      right to left: 2^3^2 = 2^9; the exponent may be signed
 *   !              factorial (gamma(x+1))
 *   numbers (1, 2.5, .5, 1e-3), names, f(args), ( ... )
 * pi and e are constants; any other name not followed by "(" is a variable.
 * Functions: sin cos tan asin acos atan sinh cosh tanh exp ln log (base 10)
 * sqrt cbrt abs floor ceil round sign, and atan2 hypot min max mod with two
 * arguments. Returns SQM_OK or the error code (err may be NULL).
 */
int sqm_parse(const char *text, SqmExpr *out, SqmError *err);

/* The name of a function ("sin"), and its argument count. */
const char *sqm_fn_name(SqmFn fn);
int sqm_fn_args(SqmFn fn);

/* Text for an error code. */
const char *sqm_strerror(int code);

/*
 * Evaluate with values for the variables: names[i] = values[i]. Returns
 * SQM_OK and the value in *result (which may be NaN or infinite, as in
 * sqrt(-1) or 1/0), or SQM_ERR_UNKNOWN when a variable has no value.
 */
int sqm_eval(const SqmExpr *e, const char *const *names, const double *values, int n, double *result);

/* 1 if the expression mentions variable name, else 0. */
int sqm_uses(const SqmExpr *e, const char *name);

/* ---- compiled programs: the same value as sqm_eval, without the tree walk ---- */

typedef struct {
    int nops;
    int stack;   /* deepest stack the program needs */
    int nslots;  /* variables it reads, by slot */
    uint8_t op[SQM_MAX_OPS];
    int16_t arg[SQM_MAX_OPS];
    double k[SQM_MAX_OPS];
} SqmProgram;

/* Compile with variables bound to slots: slots[i] is the name read from
 * values[i] by sqm_run. SQM_ERR_UNKNOWN if the expression uses another name. */
int sqm_compile(const SqmExpr *e, const char *const *slots, int nslots, SqmProgram *p);

/* Run with values[0 .. nslots-1]. */
double sqm_run(const SqmProgram *p, const double *values);

/* ---- symbolic ---- */

/* The derivative with respect to var, simplified, into out (which may not
 * be in). Other variables are held constant. SQM_ERR_DIFF for min, max,
 * mod and factorial of an expression that depends on var. */
int sqm_diff(const SqmExpr *in, const char *var, SqmExpr *out);

/* Constant folding and identities (x+0, 1*x, x^1, x-x, --x, ...) into out.
 * Numbers combine exactly where that keeps them exact: 6/3 becomes 2 but
 * 1/3 stays a fraction, sqrt(4) becomes 2 but sqrt(2) stays. */
int sqm_simplify(const SqmExpr *in, SqmExpr *out);

/* Print with few parentheses, as text that parses back to the same value:
 * "x^2 + 3*x - 1", "-(x + 1)", "(-2)^x", "-x/2", "2*x*sin(x)". Returns the length
 * the full text needs, like snprintf (out is always terminated if cap > 0). */
size_t sqm_print(const SqmExpr *e, char *out, size_t cap);

/* Expand products and whole powers into a sum of terms with exact
 * fractions: (x + 1)^2 is x^2 + 2*x + 1, (a + b)(a - b) is a^2 - b^2.
 * Parts that are not polynomials stay whole and are expanded inside:
 * sin((x+1)^2) is sin(x^2 + 2*x + 1), and (x + 1)/(x - 1) is
 * x/(x - 1) + 1/(x - 1). Terms come highest degree first. Uses about
 * 100 KB of stack at the deepest nesting. SQM_ERR_RANGE when a fraction
 * outgrows 62 bits, SQM_ERR_FULL past 64 terms or 8 distinct parts. */
int sqm_expand(const SqmExpr *in, SqmExpr *out);

/* Expand, then group the terms by powers of var:
 * a x^2 + b x^2 + c x  becomes  (a + b)*x^2 + c*x. */
int sqm_collect(const SqmExpr *in, const char *var, SqmExpr *out);

/* Parse "left = right" as left - right (plain text with no = parses as is).
 * Error positions are in the whole text. */
int sqm_parse_eq(const char *text, SqmExpr *out, SqmError *err);

typedef struct {
    char text[128]; /* "-sqrt(2)", "(1 + sqrt(5))/2", "-b/a" */
    double value;   /* NaN when it depends on other variables */
} SqmSolution;

/*
 * Solve eq = 0 for var exactly, where eq expands to a polynomial in var:
 * linear and quadratic with any coefficients (other variables stay symbols),
 * and higher degrees with number coefficients through their rational roots,
 * leaving a quadratic. Real solutions only, each once, in increasing order
 * (symbolic ones last). Writes up to cap solutions and the number in *count:
 * 0 when there is none, -1 when every value works (x - x). Returns SQM_OK,
 * or SQM_ERR_SOLVE when var is inside a function (sin(x) = 0) or a factor
 * of degree 3 or more is left; the solutions found so far are still written.
 */
int sqm_solve_for(const SqmExpr *eq, const char *var, SqmSolution *out, int cap, int *count);

/*
 * Factor over the rationals in var (NULL: the expression's only variable).
 * The expression must be a polynomial or a ratio of polynomials in var with
 * number coefficients. Common factors of the numerator and denominator
 * cancel; each is written as a number times var^k, the rational linear
 * factors (2*x - 1), and square-free parts left whole (x^2 + 1, x^4 + 1):
 * x^3 - x is x*(x - 1)*(x + 1), and (x^2 - 1)/(x^2 + 2x + 1) is
 * (x - 1)/(x + 1). SQM_ERR_NOFORM when it is not of that kind. Like
 * sqm_expand, it uses about 100 KB of stack.
 */
int sqm_factor(const SqmExpr *in, const char *var, SqmExpr *out);

/*
 * Partial fractions in var (NULL: the only variable) of a ratio of
 * polynomials with number coefficients: the polynomial part, then
 * A/(linear)^k and (B x + C)/(quadratic)^k, exactly. 1/(x^2 - 1) is
 * 1/(2*(x - 1)) - 1/(2*(x + 1)). SQM_ERR_NOFORM when the denominator keeps
 * a factor of degree 3 or more over the rationals.
 */
int sqm_apart(const SqmExpr *in, const char *var, SqmExpr *out);

/*
 * A symbolic integral in var, simplified, with no constant added: sums and
 * constant multiples; powers, e^u, b^u and sin cos tan sinh cosh tanh exp
 * ln log sqrt cbrt atan asin acos abs of a linear u = a x + b; substitution
 * f(g) k g' and g'/g; a polynomial times exp, sin or cos of a linear u, or
 * times ln(x), by parts; (B x + C)/(quadratic); and ratios of polynomials
 * through partial fractions (powers of quadratics excepted). Otherwise
 * SQM_ERR_NOFORM. Not reentrant (it keeps about 40 KB of static scratch).
 */
int sqm_antideriv(const SqmExpr *in, const char *var, SqmExpr *out);

/* ---- numeric ---- */

/* A root of the program in variable slot within [lo, hi], where it changes
 * sign: bisection safeguarded false position. The other slots keep their
 * values. Returns SQM_OK, or -1 when the ends have the same sign or a value
 * is not finite. */
int sqm_solve(const SqmProgram *p, double *values, int slot, double lo, double hi, double *root);

/* All sign-change roots in [lo, hi], scanning `steps` equal intervals.
 * Writes up to cap roots in increasing order and returns how many there are. */
int sqm_roots(const SqmProgram *p, double *values, int slot, double lo, double hi, int steps,
              double *out, int cap);

/* Integral over [a, b] by adaptive Simpson to about tol. *ok is set to 0 when
 * the recursion ran out of depth or met a non-finite value (ok may be NULL). */
double sqm_integrate(const SqmProgram *p, double *values, int slot, double a, double b, double tol, int *ok);

/* A calculator display of x: up to `digits` significant digits (1..17),
 * no trailing zeros, exponent form outside 1e-6 .. 1e15, "-0" as "0", and
 * "undefined" for anything not finite (0/0, sqrt(-1), 1/0, overflow), as
 * graphing calculators show it. Returns the length like snprintf. */
size_t sqm_format(double x, int digits, char *out, size_t cap);

/* ---- units ----
 * A quantity is a number in SI base units and the powers of the base units
 * m kg s A K mol cd bit. Expressions use the units by name, with SI
 * prefixes where they make sense (km, mA, kWh, MiB):
 *   "60 mi/h * 2.5 h"   "3 ft + 2 in"   "5 kg * 9.81 m/s^2"   "sqrt(2 m^2)"
 *   "sin(30 deg)"       "1200 W * 3 h"  "100 km/h"            "m2" = "m^2"
 * Angles have no dimension (radians); deg, gon, arcmin, arcsec, rev are
 * angle units. degC and degF are absolute temperatures and only stand
 * alone ("20 degC"); use K for differences.
 * Units include: m in ft usft yd mi nmi ch rod au ly, g t lb oz, s sec min
 * h hr d day wk yr, ha acre L l cc gal qt pt floz, mph kph kn, N lbf, J cal
 * Wh eV BTU, W hp, Pa bar atm psi mmHg torr, Hz rpm, A C Ah V ohm F T Wb H
 * S, K degC degF degR, mol cd, bit B byte, rad deg gon arcmin arcsec rev
 * percent. */
#define SQM_DIMS 8
typedef struct {
    double value;              /* in SI base units */
    signed char dim[SQM_DIMS]; /* powers of m kg s A K mol cd bit */
} SqmQty;

/* Evaluate a quantity expression. Returns SQM_OK or an error code
 * (SQM_ERR_UNITS for units that don't fit, SQM_ERR_UNKNOWN for a name that
 * is not a unit). */
int sqm_qty(const char *text, SqmQty *out, SqmError *err);
/* q in the unit `unit` (any unit expression: "km/h", "ft", "degF"). */
int sqm_qty_to(const SqmQty *q, const char *unit, double *out, SqmError *err);
/* The SI units of q: "N", "m/s^2", "kg/(m*s)", or "" with no dimension. */
size_t sqm_qty_units(const SqmQty *q, char *out, size_t cap);
/* "9.81 m/s^2": the value and its SI units. */
size_t sqm_qty_print(const SqmQty *q, int digits, char *out, size_t cap);
/* A calculator line: "EXPR", "EXPR to UNIT" (or "->"), "EXPR to dms" for an
 * angle in degrees, minutes and seconds, or "EXPR to si". out gets
 * "1.524 m", "35°40'52.45\"", ... */
int sqm_units_line(const char *text, int digits, char *out, size_t cap, SqmError *err);
/* 1 when name is a unit (with or without a prefix) */
int sqm_is_unit(const char *name);

#endif
