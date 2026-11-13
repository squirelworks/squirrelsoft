/* sqmath units: quantities with dimensions, unit conversion and unit
 * arithmetic (original code). Values are kept in SI base units; a quantity's
 * dimension is the vector of powers of m kg s A K mol cd bit. */
#include "sqmath_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the unit table ---- */

enum { PRE_NONE = 0, PRE_SI = 1, PRE_BIN = 2 }; /* which prefixes a unit takes */

typedef struct {
    const char *name;
    double scale;  /* SI value of one unit */
    double offset; /* absolute temperatures: kelvin = v * scale + offset */
    signed char dim[SQM_DIMS];
    int prefixes;
} UnitDef;

#define L_ {1, 0, 0, 0, 0, 0, 0, 0}
#define M_ {0, 1, 0, 0, 0, 0, 0, 0}
#define T_ {0, 0, 1, 0, 0, 0, 0, 0}
#define I_ {0, 0, 0, 1, 0, 0, 0, 0}
#define K_ {0, 0, 0, 0, 1, 0, 0, 0}
#define N_ {0, 0, 0, 0, 0, 1, 0, 0}
#define J_ {0, 0, 0, 0, 0, 0, 1, 0}
#define B_ {0, 0, 0, 0, 0, 0, 0, 1}
#define ONE {0, 0, 0, 0, 0, 0, 0, 0}
#define AREA {2, 0, 0, 0, 0, 0, 0, 0}
#define VOL {3, 0, 0, 0, 0, 0, 0, 0}
#define SPEED {1, 0, -1, 0, 0, 0, 0, 0}
#define FORCE {1, 1, -2, 0, 0, 0, 0, 0}
#define ENERGY {2, 1, -2, 0, 0, 0, 0, 0}
#define POWER {2, 1, -3, 0, 0, 0, 0, 0}
#define PRESS {-1, 1, -2, 0, 0, 0, 0, 0}
#define FREQ {0, 0, -1, 0, 0, 0, 0, 0}
#define CHARGE {0, 0, 1, 1, 0, 0, 0, 0}
#define VOLT {2, 1, -3, -1, 0, 0, 0, 0}
#define OHM {2, 1, -3, -2, 0, 0, 0, 0}
#define FARAD {-2, -1, 4, 2, 0, 0, 0, 0}
#define TESLA {0, 1, -2, -1, 0, 0, 0, 0}
#define WEBER {2, 1, -2, -1, 0, 0, 0, 0}
#define HENRY {2, 1, -2, -2, 0, 0, 0, 0}
#define SIEMENS {-2, -1, 3, 2, 0, 0, 0, 0}

#define FT 0.3048
#define LB 0.45359237
#define GAL 3.785411784e-3
#define USFT (1200.0 / 3937.0)

static const UnitDef UNITS[] = {
    /* length */
    {"m", 1, 0, L_, PRE_SI},
    {"in", 0.0254, 0, L_, 0},
    {"ft", FT, 0, L_, 0},
    {"usft", USFT, 0, L_, 0}, /* US survey foot */
    {"yd", 0.9144, 0, L_, 0},
    {"mi", 1609.344, 0, L_, 0},
    {"nmi", 1852, 0, L_, 0},
    {"ch", 66 * USFT, 0, L_, 0}, /* Gunter's chain */
    {"rod", 16.5 * USFT, 0, L_, 0},
    {"au", 149597870700.0, 0, L_, 0},
    {"ly", 9460730472580800.0, 0, L_, 0},
    /* mass */
    {"g", 1e-3, 0, M_, PRE_SI},
    {"t", 1000, 0, M_, 0},
    {"lb", LB, 0, M_, 0},
    {"oz", LB / 16, 0, M_, 0},
    /* time */
    {"s", 1, 0, T_, PRE_SI},
    {"sec", 1, 0, T_, 0},
    {"min", 60, 0, T_, 0},
    {"h", 3600, 0, T_, 0},
    {"hr", 3600, 0, T_, 0},
    {"d", 86400, 0, T_, 0},
    {"day", 86400, 0, T_, 0},
    {"wk", 604800, 0, T_, 0},
    {"yr", 31557600, 0, T_, 0}, /* Julian year */
    /* area and volume */
    {"ha", 1e4, 0, AREA, 0},
    {"acre", 4046.8564224, 0, AREA, 0},
    {"L", 1e-3, 0, VOL, PRE_SI},
    {"l", 1e-3, 0, VOL, PRE_SI},
    {"cc", 1e-6, 0, VOL, 0},
    {"gal", GAL, 0, VOL, 0},
    {"qt", GAL / 4, 0, VOL, 0},
    {"pt", GAL / 8, 0, VOL, 0},
    {"floz", GAL / 128, 0, VOL, 0},
    /* speed */
    {"mph", 1609.344 / 3600, 0, SPEED, 0},
    {"kph", 1000.0 / 3600, 0, SPEED, 0},
    {"kn", 1852.0 / 3600, 0, SPEED, 0},
    /* mechanics */
    {"N", 1, 0, FORCE, PRE_SI},
    {"lbf", LB * 9.80665, 0, FORCE, 0},
    {"J", 1, 0, ENERGY, PRE_SI},
    {"cal", 4.184, 0, ENERGY, PRE_SI},
    {"Wh", 3600, 0, ENERGY, PRE_SI},
    {"eV", 1.602176634e-19, 0, ENERGY, PRE_SI},
    {"BTU", 1055.05585262, 0, ENERGY, 0},
    {"W", 1, 0, POWER, PRE_SI},
    {"hp", 745.69987158227022, 0, POWER, 0},
    {"Pa", 1, 0, PRESS, PRE_SI},
    {"bar", 1e5, 0, PRESS, PRE_SI},
    {"atm", 101325, 0, PRESS, 0},
    {"psi", LB * 9.80665 / (0.0254 * 0.0254), 0, PRESS, 0},
    {"mmHg", 133.322387415, 0, PRESS, 0},
    {"torr", 101325.0 / 760, 0, PRESS, 0},
    {"Hz", 1, 0, FREQ, PRE_SI},
    {"rpm", 1.0 / 60, 0, FREQ, 0},
    /* electricity */
    {"A", 1, 0, I_, PRE_SI},
    {"C", 1, 0, CHARGE, PRE_SI},
    {"Ah", 3600, 0, CHARGE, PRE_SI},
    {"V", 1, 0, VOLT, PRE_SI},
    {"ohm", 1, 0, OHM, PRE_SI},
    {"\xce\xa9", 1, 0, OHM, PRE_SI}, /* Ω */
    {"F", 1, 0, FARAD, PRE_SI},
    {"T", 1, 0, TESLA, PRE_SI},
    {"Wb", 1, 0, WEBER, PRE_SI},
    {"H", 1, 0, HENRY, PRE_SI},
    {"S", 1, 0, SIEMENS, PRE_SI},
    /* temperature: K for differences; degC, degF absolute (alone only) */
    {"K", 1, 0, K_, PRE_SI},
    {"degC", 1, 273.15, K_, 0},
    {"\xc2\xb0" "C", 1, 273.15, K_, 0},
    {"degF", 5.0 / 9, 459.67 * 5 / 9, K_, 0},
    {"\xc2\xb0" "F", 5.0 / 9, 459.67 * 5 / 9, K_, 0},
    {"degR", 5.0 / 9, 0, K_, 0},
    /* amount, light, information */
    {"mol", 1, 0, N_, PRE_SI},
    {"cd", 1, 0, J_, PRE_SI},
    {"bit", 1, 0, B_, PRE_SI | PRE_BIN},
    {"B", 8, 0, B_, PRE_SI | PRE_BIN},
    {"byte", 8, 0, B_, 0},
    /* angles have no dimension: radians */
    {"rad", 1, 0, ONE, PRE_SI},
    {"deg", SQM_PI / 180, 0, ONE, 0},
    {"\xc2\xb0", SQM_PI / 180, 0, ONE, 0}, /* ° */
    {"gon", SQM_PI / 200, 0, ONE, 0},
    {"arcmin", SQM_PI / 10800, 0, ONE, 0},
    {"arcsec", SQM_PI / 648000, 0, ONE, 0},
    {"rev", 2 * SQM_PI, 0, ONE, 0},
    {"percent", 0.01, 0, ONE, 0},
};
#define N_UNITS (int)(sizeof UNITS / sizeof UNITS[0])

static const struct {
    const char *p;
    double f;
    int kind;
} PREFIXES[] = {
    {"Ki", 1024.0, PRE_BIN}, {"Mi", 1048576.0, PRE_BIN}, {"Gi", 1073741824.0, PRE_BIN},
    {"Ti", 1099511627776.0, PRE_BIN},
    {"da", 1e1, PRE_SI}, {"Y", 1e24, PRE_SI}, {"Z", 1e21, PRE_SI}, {"E", 1e18, PRE_SI}, {"P", 1e15, PRE_SI},
    {"T", 1e12, PRE_SI}, {"G", 1e9, PRE_SI}, {"M", 1e6, PRE_SI}, {"k", 1e3, PRE_SI}, {"h", 1e2, PRE_SI},
    {"d", 1e-1, PRE_SI}, {"c", 1e-2, PRE_SI}, {"m", 1e-3, PRE_SI}, {"u", 1e-6, PRE_SI},
    {"\xc2\xb5", 1e-6, PRE_SI}, /* µ */
    {"n", 1e-9, PRE_SI}, {"p", 1e-12, PRE_SI}, {"f", 1e-15, PRE_SI}, {"a", 1e-18, PRE_SI},
};

/* the unit named by s[0..len), exactly or as prefix + unit */
static int lookup(const char *s, size_t len, double *scale, double *offset, signed char *dim) {
    for (int i = 0; i < N_UNITS; i++)
        if (strlen(UNITS[i].name) == len && !memcmp(UNITS[i].name, s, len)) {
            *scale = UNITS[i].scale;
            *offset = UNITS[i].offset;
            memcpy(dim, UNITS[i].dim, SQM_DIMS);
            return 1;
        }
    for (size_t k = 0; k < sizeof PREFIXES / sizeof PREFIXES[0]; k++) {
        size_t pl = strlen(PREFIXES[k].p);
        if (pl >= len || memcmp(PREFIXES[k].p, s, pl)) continue;
        for (int i = 0; i < N_UNITS; i++)
            if ((UNITS[i].prefixes & PREFIXES[k].kind) && strlen(UNITS[i].name) == len - pl &&
                !memcmp(UNITS[i].name, s + pl, len - pl)) {
                *scale = UNITS[i].scale * PREFIXES[k].f;
                *offset = 0;
                memcpy(dim, UNITS[i].dim, SQM_DIMS);
                return 1;
            }
    }
    return 0;
}

/* ---- the quantity parser ---- */

typedef struct {
    const char *s, *p;
    int depth;
    int offset_unit; /* an absolute temperature unit was read */
    double offset;
    SqmError *err;
} Ps;

static int fail(Ps *ps, int code, const char *at, const char *msg) {
    if (ps->err && ps->err->code == SQM_OK) {
        ps->err->code = code;
        ps->err->pos = (int)(at - ps->s);
        snprintf(ps->err->msg, sizeof ps->err->msg, "%s", msg);
    }
    return code;
}

static void skip(Ps *ps) {
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

static int is_name_byte(unsigned char c) { return isalpha(c) || c == '_' || c >= 0x80; }

static int same_dim(const SqmQty *a, const SqmQty *b) { return !memcmp(a->dim, b->dim, SQM_DIMS); }

static int dimensionless(const SqmQty *q) {
    for (int k = 0; k < SQM_DIMS; k++)
        if (q->dim[k]) return 0;
    return 1;
}

static int expr(Ps *ps, SqmQty *out);

static int qpow(Ps *ps, SqmQty *q, double n, const char *at) {
    if (!dimensionless(q)) {
        if (n != floor(n)) return fail(ps, SQM_ERR_UNITS, at, "a unit to a power must be whole");
        if (fabs(n) > 100) return fail(ps, SQM_ERR_RANGE, at, "unit power too large");
        for (int k = 0; k < SQM_DIMS; k++) {
            int d = q->dim[k] * (int)n;
            if (d > 100 || d < -100) return fail(ps, SQM_ERR_RANGE, at, "unit power too large");
            q->dim[k] = (signed char)d;
        }
    }
    q->value = pow(q->value, n);
    return SQM_OK;
}

static int qroot(Ps *ps, SqmQty *q, int n, const char *at) {
    for (int k = 0; k < SQM_DIMS; k++)
        if (q->dim[k] % n) return fail(ps, SQM_ERR_UNITS, at, n == 2 ? "no square root of these units" : "no cube root of these units");
    for (int k = 0; k < SQM_DIMS; k++) q->dim[k] = (signed char)(q->dim[k] / n);
    q->value = n == 2 ? sqrt(q->value) : cbrt(q->value);
    return SQM_OK;
}

static const struct {
    const char *name;
    int fn;
} QFN[] = {
    {"sqrt", SQM_FN_SQRT}, {"cbrt", SQM_FN_CBRT}, {"abs", SQM_FN_ABS},   {"sin", SQM_FN_SIN},
    {"cos", SQM_FN_COS},   {"tan", SQM_FN_TAN},   {"asin", SQM_FN_ASIN}, {"acos", SQM_FN_ACOS},
    {"atan", SQM_FN_ATAN}, {"exp", SQM_FN_EXP},   {"ln", SQM_FN_LN},     {"log", SQM_FN_LOG},
    {"round", SQM_FN_ROUND}, {"floor", SQM_FN_FLOOR}, {"ceil", SQM_FN_CEIL},
};

static int primary(Ps *ps, SqmQty *out) {
    skip(ps);
    const char *at = ps->p;
    memset(out, 0, sizeof *out);
    unsigned char c = (unsigned char)*ps->p;
    if (isdigit(c) || (c == '.' && isdigit((unsigned char)ps->p[1]))) {
        /* a number: digits, a fraction, an exponent (not the start of a unit like "e" alone) */
        const char *q = ps->p;
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '.') {
            q++;
            while (isdigit((unsigned char)*q)) q++;
        }
        if ((*q == 'e' || *q == 'E') &&
            (isdigit((unsigned char)q[1]) || ((q[1] == '-' || q[1] == '+') && isdigit((unsigned char)q[2])))) {
            q += 2;
            while (isdigit((unsigned char)*q)) q++;
        }
        char buf[64];
        size_t n = (size_t)(q - ps->p);
        if (n >= sizeof buf) return fail(ps, SQM_ERR_SYNTAX, at, "number too long");
        memcpy(buf, ps->p, n);
        buf[n] = 0;
        out->value = strtod(buf, NULL);
        ps->p = q;
        return SQM_OK;
    }
    if (c == '(') {
        if (++ps->depth > SQM_MAX_DEPTH) return fail(ps, SQM_ERR_DEPTH, at, "nested too deeply");
        ps->p++;
        int rc = expr(ps, out);
        if (rc) return rc;
        skip(ps);
        if (*ps->p != ')') return fail(ps, SQM_ERR_SYNTAX, ps->p, "missing )");
        ps->p++;
        ps->depth--;
        return SQM_OK;
    }
    if (is_name_byte(c)) {
        const char *q = ps->p;
        while (is_name_byte((unsigned char)*q)) {
            /* a degree sign followed by C or F is one name; on its own it is degrees */
            q++;
        }
        size_t len = (size_t)(q - ps->p);
        const char *name = ps->p;
        ps->p = q;
        skip(ps);
        if (*ps->p == '(') {
            for (size_t k = 0; k < sizeof QFN / sizeof QFN[0]; k++) {
                if (strlen(QFN[k].name) != len || memcmp(QFN[k].name, name, len)) continue;
                const char *arg_at = ps->p;
                int rc = primary(ps, out);
                if (rc) return rc;
                if (QFN[k].fn == SQM_FN_SQRT) return qroot(ps, out, 2, arg_at);
                if (QFN[k].fn == SQM_FN_CBRT) return qroot(ps, out, 3, arg_at);
                if (QFN[k].fn == SQM_FN_ABS) return out->value = fabs(out->value), SQM_OK;
                if (!dimensionless(out)) return fail(ps, SQM_ERR_UNITS, arg_at, "needs a number or an angle");
                out->value = sqm__apply(QFN[k].fn, out->value, 0);
                return SQM_OK;
            }
            return fail(ps, SQM_ERR_UNKNOWN, name, "not a function");
        }
        if (len == 2 && !memcmp(name, "pi", 2)) {
            out->value = SQM_PI;
            return SQM_OK;
        }
        double scale, offset;
        if (len >= SQM_NAME_LEN) return fail(ps, SQM_ERR_NAME, name, "name too long");
        if (!lookup(name, len, &scale, &offset, out->dim)) return fail(ps, SQM_ERR_UNKNOWN, name, "unknown unit");
        out->value = scale;
        if (offset != 0) {
            ps->offset_unit++;
            ps->offset = offset;
        }
        /* m2, s3: a power written straight after the name */
        if (q == ps->p && isdigit((unsigned char)*q)) {
            int n = 0;
            while (isdigit((unsigned char)*ps->p) && n < 100) n = n * 10 + (*ps->p++ - '0');
            return qpow(ps, out, n, name);
        }
        return SQM_OK;
    }
    if (!c) return fail(ps, SQM_ERR_SYNTAX, at, "expression ends too soon");
    return fail(ps, SQM_ERR_SYNTAX, at, "unexpected character");
}

static int unary(Ps *ps, SqmQty *out);

static int power(Ps *ps, SqmQty *out) {
    int rc = primary(ps, out);
    if (rc) return rc;
    skip(ps);
    if (*ps->p == '^' || (ps->p[0] == '*' && ps->p[1] == '*')) {
        ps->p += *ps->p == '^' ? 1 : 2;
        const char *at = ps->p;
        SqmQty e;
        rc = unary(ps, &e); /* right to left, and signed */
        if (rc) return rc;
        if (!dimensionless(&e)) return fail(ps, SQM_ERR_UNITS, at, "an exponent has no units");
        return qpow(ps, out, e.value, at);
    }
    return SQM_OK;
}

static int unary(Ps *ps, SqmQty *out) {
    skip(ps);
    if (*ps->p == '-' || *ps->p == '+') {
        int neg = *ps->p == '-';
        ps->p++;
        int rc = unary(ps, out);
        if (neg) out->value = -out->value;
        return rc;
    }
    return power(ps, out);
}

static int starts_primary(const char *p) {
    unsigned char c = (unsigned char)*p;
    return isdigit(c) || c == '.' || c == '(' || is_name_byte(c);
}

static int combine(Ps *ps, SqmQty *out, const SqmQty *r, int div) {
    for (int k = 0; k < SQM_DIMS; k++) {
        int d = div ? out->dim[k] - r->dim[k] : out->dim[k] + r->dim[k];
        if (d > 100 || d < -100) return fail(ps, SQM_ERR_RANGE, ps->p, "unit power too large");
        out->dim[k] = (signed char)d;
    }
    out->value = div ? out->value / r->value : out->value * r->value;
    return SQM_OK;
}

static int at_to(const char *p) { return !strncmp(p, "to ", 3) || !strcmp(p, "to"); }

/* side by side binds tighter than * and /: "2 V / 1 ohm" is 2 V / (1 ohm),
 * "kg m^2/s^2" is (kg m^2)/s^2 */
static int group(Ps *ps, SqmQty *out) {
    int rc = unary(ps, out);
    while (!rc) {
        skip(ps);
        if (!starts_primary(ps->p) || at_to(ps->p)) break;
        SqmQty r;
        if ((rc = unary(ps, &r))) break;
        rc = combine(ps, out, &r, 0);
    }
    return rc;
}

static int term(Ps *ps, SqmQty *out) {
    int rc = group(ps, out);
    while (!rc) {
        skip(ps);
        int div = 0;
        if (*ps->p == '/') div = 1, ps->p++;
        else if (*ps->p == '*' && ps->p[1] != '*') ps->p++;
        else if (!strncmp(ps->p, "\xc2\xb7", 2)) ps->p += 2; /* · */
        else break;
        SqmQty r;
        if ((rc = group(ps, &r))) break;
        rc = combine(ps, out, &r, div);
    }
    return rc;
}

static int expr(Ps *ps, SqmQty *out) {
    int rc = term(ps, out);
    while (!rc) {
        skip(ps);
        if (*ps->p != '+' && *ps->p != '-') break;
        int sub = *ps->p == '-';
        const char *at = ps->p++;
        SqmQty r;
        if ((rc = term(ps, &r))) break;
        if (!same_dim(out, &r)) return fail(ps, SQM_ERR_UNITS, at, "adding different kinds of units");
        out->value = sub ? out->value - r.value : out->value + r.value;
    }
    return rc;
}

/* text[0..len) as a quantity; *offset_unit and *offset tell of an absolute
 * temperature unit */
static int parse_qty(const char *text, size_t len, SqmQty *out, int *offset_unit, double *offset, SqmError *err) {
    char buf[512];
    if (len >= sizeof buf) {
        if (err) *err = (SqmError){SQM_ERR_FULL, 0, "expression too long"};
        return SQM_ERR_FULL;
    }
    memcpy(buf, text, len);
    buf[len] = 0;
    Ps ps = {buf, buf, 0, 0, 0, err};
    int rc = expr(&ps, out);
    if (!rc) {
        skip(&ps);
        if (*ps.p) rc = fail(&ps, SQM_ERR_SYNTAX, ps.p, "unexpected text");
    }
    *offset_unit = ps.offset_unit;
    *offset = ps.offset;
    return rc;
}

/* ---- the public part ---- */

int sqm_qty(const char *text, SqmQty *out, SqmError *err) {
    if (err) *err = (SqmError){SQM_OK, 0, ""};
    int ou;
    double off;
    int rc = parse_qty(text, strlen(text), out, &ou, &off, err);
    if (rc) return rc;
    if (ou) {
        /* degC and degF only as "NUMBER degC": then an absolute temperature */
        const char *p = text, *num;
        while (*p == ' ') p++;
        if (*p == '-' || *p == '+') p++;
        num = p;
        while (isdigit((unsigned char)*p) || *p == '.') p++;
        if ((*p == 'e' || *p == 'E') && p > num && (isdigit((unsigned char)p[1]) || p[1] == '-' || p[1] == '+')) {
            p += 2;
            while (isdigit((unsigned char)*p)) p++;
        }
        int has_num = p > num;
        while (*p == ' ') p++;
        size_t len = strlen(p);
        while (len && p[len - 1] == ' ') len--;
        double scale, o;
        signed char d[SQM_DIMS];
        if (!has_num || !lookup(p, len, &scale, &o, d) || o == 0) {
            if (err) *err = (SqmError){SQM_ERR_UNITS, (int)(p - text), "degC/degF only alone; use K for differences"};
            return SQM_ERR_UNITS;
        }
        out->value += off;
    }
    return SQM_OK;
}

int sqm_qty_to(const SqmQty *q, const char *unit, double *out, SqmError *err) {
    if (err) *err = (SqmError){SQM_OK, 0, ""};
    SqmQty u;
    int ou;
    double off;
    int rc = parse_qty(unit, strlen(unit), &u, &ou, &off, err);
    if (rc) return rc;
    if (ou > 1) {
        if (err) *err = (SqmError){SQM_ERR_UNITS, 0, "degC/degF only alone"};
        return SQM_ERR_UNITS;
    }
    if (!same_dim(q, &u)) {
        if (err) *err = (SqmError){SQM_ERR_UNITS, 0, "units don't match"};
        return SQM_ERR_UNITS;
    }
    if (u.value == 0) {
        if (err) *err = (SqmError){SQM_ERR_RANGE, 0, "a zero unit"};
        return SQM_ERR_RANGE;
    }
    *out = (q->value - (ou ? off : 0)) / u.value;
    return SQM_OK;
}

static const struct {
    const char *name;
    signed char dim[SQM_DIMS];
} NAMED[] = {
    {"N", FORCE}, {"J", ENERGY}, {"W", POWER}, {"Pa", PRESS}, {"Hz", FREQ},   {"C", CHARGE}, {"V", VOLT},
    {"ohm", OHM}, {"F", FARAD},  {"T", TESLA}, {"Wb", WEBER}, {"H", HENRY}, {"S", SIEMENS},
};
static const char *const BASE[SQM_DIMS] = {"m", "kg", "s", "A", "K", "mol", "cd", "bit"};

size_t sqm_qty_units(const SqmQty *q, char *out, size_t cap) {
    if (cap) out[0] = 0;
    for (size_t k = 0; k < sizeof NAMED / sizeof NAMED[0]; k++)
        if (!memcmp(NAMED[k].dim, q->dim, SQM_DIMS)) return (size_t)snprintf(out, cap, "%s", NAMED[k].name);
    /* kg first, then m, s, ...; the negative powers after a slash */
    static const int order[SQM_DIMS] = {1, 0, 2, 3, 4, 5, 6, 7};
    size_t at = 0;
    int n_neg = 0, any_pos = 0;
    for (int i = 0; i < SQM_DIMS; i++) n_neg += q->dim[i] < 0;
    for (int pass = 0; pass < 2; pass++) {
        int first = 1;
        if (pass == 1 && n_neg && !any_pos) {
            /* no positive powers: m^-2, s^-1*K^-1 */
            for (int j = 0; j < SQM_DIMS; j++) {
                int k = order[j];
                if (q->dim[k] >= 0) continue;
                at += (size_t)snprintf(out + (at < cap ? at : cap), at < cap ? cap - at : 0, "%s%s^%d",
                                       first ? "" : "*", BASE[k], q->dim[k]);
                first = 0;
            }
            break;
        }
        if (pass == 1 && n_neg)
            at += (size_t)snprintf(out + (at < cap ? at : cap), at < cap ? cap - at : 0, n_neg > 1 ? "/(" : "/");
        for (int i = 0; i < SQM_DIMS; i++) {
            int k = order[i], d = q->dim[k];
            if (pass == 0 ? d <= 0 : d >= 0) continue;
            d = d < 0 ? -d : d;
            any_pos |= pass == 0;
            char *w = out + (at < cap ? at : cap);
            size_t room = at < cap ? cap - at : 0;
            at += (size_t)(d == 1 ? snprintf(w, room, "%s%s", first ? "" : "*", BASE[k])
                                  : snprintf(w, room, "%s%s^%d", first ? "" : "*", BASE[k], d));
            first = 0;
        }
        if (pass == 1 && n_neg > 1) at += (size_t)snprintf(out + (at < cap ? at : cap), at < cap ? cap - at : 0, ")");
    }
    return at;
}

size_t sqm_qty_print(const SqmQty *q, int digits, char *out, size_t cap) {
    char num[64], units[96];
    sqm_format(q->value, digits, num, sizeof num);
    sqm_qty_units(q, units, sizeof units);
    return (size_t)snprintf(out, cap, units[0] ? "%s %s" : "%s", num, units);
}

/* "35°40'52.45"" for an angle */
static size_t dms(double rad, char *out, size_t cap) {
    double deg = rad * 180 / SQM_PI;
    int neg = deg < 0;
    double a = fabs(deg);
    double total = floor(a * 360000 + 0.5); /* hundredths of a second */
    long d = (long)(total / 360000);
    long m = (long)((total - d * 360000.0) / 6000);
    double s = (total - d * 360000.0 - m * 6000.0) / 100;
    return (size_t)snprintf(out, cap, "%s%ld\xc2\xb0%02ld'%05.2f\"", neg ? "-" : "", d, m, s);
}

/* the last " to " (or "->") outside parentheses */
static const char *find_to(const char *text, size_t *skiplen) {
    const char *best = NULL;
    int depth = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        else if (depth == 0 && p > text && p[-1] == ' ' && !strncmp(p, "to ", 3)) best = p, *skiplen = 3;
        else if (depth == 0 && !strncmp(p, "->", 2)) best = p, *skiplen = 2;
    }
    return best;
}

int sqm_units_line(const char *text, int digits, char *out, size_t cap, SqmError *err) {
    if (err) *err = (SqmError){SQM_OK, 0, ""};
    if (cap) out[0] = 0;
    size_t sl = 0;
    const char *to = find_to(text, &sl);
    char left[512];
    size_t ll = to ? (size_t)(to - text) : strlen(text);
    if (ll >= sizeof left) {
        if (err) *err = (SqmError){SQM_ERR_FULL, 0, "expression too long"};
        return SQM_ERR_FULL;
    }
    memcpy(left, text, ll);
    left[ll] = 0;
    SqmQty q;
    int rc = sqm_qty(left, &q, err);
    if (rc) return rc;
    if (!to) {
        sqm_qty_print(&q, digits, out, cap);
        return SQM_OK;
    }
    const char *target = to + sl;
    while (*target == ' ') target++;
    size_t tl = strlen(target);
    while (tl && target[tl - 1] == ' ') tl--;
    char tbuf[128];
    if (tl == 0 || tl >= sizeof tbuf) {
        if (err) *err = (SqmError){SQM_ERR_SYNTAX, (int)(target - text), "no unit to convert to"};
        return SQM_ERR_SYNTAX;
    }
    memcpy(tbuf, target, tl);
    tbuf[tl] = 0;
    if (!strcmp(tbuf, "dms")) {
        if (!dimensionless(&q)) {
            if (err) *err = (SqmError){SQM_ERR_UNITS, (int)(target - text), "dms is for angles"};
            return SQM_ERR_UNITS;
        }
        dms(q.value, out, cap);
        return SQM_OK;
    }
    if (!strcmp(tbuf, "si") || !strcmp(tbuf, "SI")) {
        sqm_qty_print(&q, digits, out, cap);
        return SQM_OK;
    }
    double v;
    rc = sqm_qty_to(&q, tbuf, &v, err);
    if (rc) {
        if (err) err->pos += (int)(target - text);
        return rc;
    }
    char num[64];
    sqm_format(v, digits, num, sizeof num);
    snprintf(out, cap, "%s %s", num, tbuf);
    return SQM_OK;
}

int sqm_is_unit(const char *name) {
    double s, o;
    signed char d[SQM_DIMS];
    return lookup(name, strlen(name), &s, &o, d);
}
