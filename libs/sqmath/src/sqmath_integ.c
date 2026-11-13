/* sqmath: symbolic integrals (see sqmath.h). Original code.
 *
 * The integral is built as text, rule by rule over the expression tree, then
 * parsed and simplified. Rules, tried in this order at each node:
 *   - constants, sums, constant multiples
 *   - powers and the function table of a linear argument a x + b
 *   - substitution: f(g(x)) * k g'(x), and g'(x)/g(x) as ln|g(x)|
 *   - (B x + C)/(quadratic) with number coefficients: ln and atan (or ln)
 *   - by parts for a polynomial times exp, sin or cos of a linear argument,
 *     and a polynomial times ln(x)
 *   - then the expanded form, and partial fractions for a ratio of
 *     polynomials
 * Nothing is guessed: what no rule covers is SQM_ERR_NOFORM. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sqmath.h"
#include "sqmath_internal.h"

#define T 768        /* text of one part */
#define MAXDEPTH 24  /* nesting followed */
#define MAXDERIV 10  /* derivatives tried for "is a polynomial" */

typedef struct {
    const SqmExpr *in;
    const char *var;
    int vi; /* the var's name index in in, or -1 */
    int code;
    int depth;
    SqmExpr t1, t2, t3; /* scratch: never live across a recursive call */
} IC;

/* formatted text into out (cap bytes); -1 when it did not fit */
static int outn(char *out, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

static int no(IC *c, int code) {
    if (!c->code) c->code = code;
    return -1;
}

static int uses(IC *c, const SqmExpr *e, int i) {
    int k = e == c->in ? c->vi : sqm__find_name(e, c->var);
    return sqm__uses_index(e, i, k);
}

/* node i of e, alone, into dst */
static int copy(IC *c, SqmExpr *dst, const SqmExpr *e, int i) {
    sqm__clear(dst);
    int r = sqm__graft(dst, e, i);
    if (r < 0) return no(c, SQM_ERR_FULL);
    dst->root = r;
    return 0;
}

static int print(IC *c, const SqmExpr *e, char *out) {
    if (sqm_print(e, out, T) >= T) return no(c, SQM_ERR_FULL);
    return 0;
}

/* the text of node i of e */
static int text_of(IC *c, const SqmExpr *e, int i, char *out) {
    if (copy(c, &c->t1, e, i)) return -1;
    return print(c, &c->t1, out);
}

/* parse text into dst, simplified */
static int parse_simple(IC *c, const char *text, SqmExpr *dst) {
    if (sqm_parse(text, &c->t3, NULL) != SQM_OK) return no(c, SQM_ERR_FULL);
    int rc = sqm_simplify(&c->t3, dst);
    if (rc) return no(c, rc);
    return 0;
}

/* d/dvar of text, simplified, as text; *constant set when it has no var */
static int deriv_text(IC *c, const char *text, char *out, int *constant) {
    if (sqm_parse(text, &c->t1, NULL) != SQM_OK) return no(c, SQM_ERR_FULL);
    int rc = sqm_diff(&c->t1, c->var, &c->t2);
    if (rc) return no(c, rc);
    if (constant) *constant = !sqm_uses(&c->t2, c->var);
    return print(c, &c->t2, out);
}

/* is the text the number 0 */
static int is_zero(const char *t) { return !strcmp(t, "0"); }

/* node i is a x + b: its slope as text */
static int linear(IC *c, const SqmExpr *e, int i, char *slope) {
    char u[T];
    int k;
    if (!uses(c, e, i) || text_of(c, e, i, u) || deriv_text(c, u, slope, &k)) {
        c->code = SQM_OK;
        return 0;
    }
    return k && !is_zero(slope);
}

/* text / other is a constant (after simplifying): that constant as text */
static int ratio_const(IC *c, const char *num, const char *den, char *k) {
    char t[2 * T + 16];
    outn(t, sizeof t, "(%s)/(%s)", num, den);
    if (parse_simple(c, t, &c->t2)) {
        c->code = SQM_OK;
        return 0;
    }
    if (sqm_uses(&c->t2, c->var)) { /* x/(2*x) cancels when expanded */
        if (sqm_parse(t, &c->t1, NULL) != SQM_OK || sqm_expand(&c->t1, &c->t2) != SQM_OK) return 0;
        if (sqm_uses(&c->t2, c->var)) return 0;
    }
    if (print(c, &c->t2, k)) {
        c->code = SQM_OK;
        return 0;
    }
    return 1;
}

/* the value of text at var = x (other names none): NAN if not a number */
static double value_at(IC *c, const char *text, double x) {
    double v;
    if (sqm_parse(text, &c->t1, NULL) != SQM_OK) return NAN;
    const char *names[1] = {c->var};
    if (sqm_eval(&c->t1, names, &x, 1, &v) != SQM_OK) return NAN;
    return v;
}

/* a short exact form for a number: p/q with q <= 1000, else 17 digits */
static void num_text(double v, char *out, size_t cap) {
    for (int q = 1; q <= 1000; q++) {
        double p = nearbyint(v * q);
        if (fabs(p) < 1e15 && fabs(v * q - p) <= 1e-9 * fmax(1, fabs(p))) {
            if (q == 1) snprintf(out, cap, "(%.0f)", p);
            else snprintf(out, cap, "(%.0f/%d)", p, q);
            return;
        }
    }
    snprintf(out, cap, "(%.17g)", v);
}

/* the derivatives of a polynomial in var, P, P', ... up to the last nonzero;
 * their number, or 0 when it is not a polynomial within MAXDERIV */
static int poly_derivs(IC *c, const char *p, char d[MAXDERIV + 1][T]) {
    snprintf(d[0], T, "%s", p);
    for (int k = 0; k < MAXDERIV; k++) {
        int constant;
        if (deriv_text(c, d[k], d[k + 1], &constant)) {
            c->code = SQM_OK;
            return 0;
        }
        if (is_zero(d[k + 1])) return k + 1;
    }
    return 0;
}

static int integ(IC *c, const SqmExpr *e, int i, char *out);

/* F(u) for the table of one-argument functions (u stands for the argument) */
static int table(int fn, const char *u, char *out) {
    switch (fn) {
        case SQM_FN_SIN: outn(out, T, "-cos(%s)", u); return 0;
        case SQM_FN_COS: outn(out, T, "sin(%s)", u); return 0;
        case SQM_FN_TAN: outn(out, T, "-ln(abs(cos(%s)))", u); return 0;
        case SQM_FN_EXP: outn(out, T, "exp(%s)", u); return 0;
        case SQM_FN_SINH: outn(out, T, "cosh(%s)", u); return 0;
        case SQM_FN_COSH: outn(out, T, "sinh(%s)", u); return 0;
        case SQM_FN_TANH: outn(out, T, "ln(cosh(%s))", u); return 0;
        case SQM_FN_LN: outn(out, T, "(%s)*ln(%s) - (%s)", u, u, u); return 0;
        case SQM_FN_LOG: outn(out, T, "((%s)*ln(%s) - (%s))/ln(10)", u, u, u); return 0;
        case SQM_FN_SQRT: outn(out, T, "2*(%s)^(3/2)/3", u); return 0;
        case SQM_FN_CBRT: outn(out, T, "3*(%s)^(4/3)/4", u); return 0;
        case SQM_FN_ATAN: outn(out, T, "(%s)*atan(%s) - ln(1 + (%s)^2)/2", u, u, u); return 0;
        case SQM_FN_ASIN: outn(out, T, "(%s)*asin(%s) + sqrt(1 - (%s)^2)", u, u, u); return 0;
        case SQM_FN_ACOS: outn(out, T, "(%s)*acos(%s) - sqrt(1 - (%s)^2)", u, u, u); return 0;
        case SQM_FN_ABS: outn(out, T, "(%s)*abs(%s)/2", u, u); return 0;
        default: return -1;
    }
}

/* F(u) for node i seen as an outer function of an inner part u (node *inner):
 * fn(u), e^u, b^u, u^n. Returns 0 with the inner node, or -1. */
static int outer(IC *c, const SqmExpr *e, int i, int *inner, char *F) {
    const SqmNode *nd = &e->node[i];
    char u[T], n[T];
    if (nd->kind == SQM_FUN && sqm_fn_args(nd->fn) == 1) {
        if (text_of(c, e, nd->a, u)) return -1;
        *inner = nd->a;
        return table(nd->fn, u, F);
    }
    if (nd->kind == SQM_POW) {
        if (!uses(c, e, nd->b)) { /* u^n */
            if (text_of(c, e, nd->a, u) || text_of(c, e, nd->b, n)) return -1;
            *inner = nd->a;
            double nv = value_at(c, n, 0);
            if (nv == -1) outn(F, T, "ln(abs(%s))", u);
            else outn(F, T, "(%s)^((%s) + 1)/((%s) + 1)", u, n, n);
            return 0;
        }
        if (!uses(c, e, nd->a)) { /* b^u */
            if (text_of(c, e, nd->a, n) || text_of(c, e, nd->b, u)) return -1;
            *inner = nd->b;
            if (e->node[nd->a].kind == SQM_CONST && e->node[nd->a].a == 1) outn(F, T, "e^(%s)", u);
            else outn(F, T, "(%s)^(%s)/ln(%s)", n, u, n);
            return 0;
        }
    }
    return -1;
}

/* F(g) with h = k g' (h as text): k F(g) */
static int sub_core(IC *c, const char *F, const char *g, const char *hh, char *out) {
    char dg[T], k[T];
    if (deriv_text(c, g, dg, NULL) || is_zero(dg)) {
        c->code = SQM_OK;
        return -1;
    }
    if (ratio_const(c, hh, dg, k)) return outn(out, T, "(%s)*(%s)", k, F);
    c->code = SQM_OK;
    return -1;
}

/* f(g) * h where h = k g': k F(g); also g * h with h = k g': k g^2/2 */
static int substitution(IC *c, const SqmExpr *e, int f, const char *hh, char *out) {
    char F[T], g[T];
    int inner;
    if (outer(c, e, f, &inner, F) == 0 && text_of(c, e, inner, g) == 0 && sub_core(c, F, g, hh, out) == 0) return 0;
    c->code = SQM_OK;
    if (text_of(c, e, f, g) || outn(F, T, "(%s)^2/2", g)) {
        c->code = SQM_OK;
        return -1;
    }
    return sub_core(c, F, g, hh, out);
}

/* e^(p x + q) times sin or cos of (r x + t) */
static int exp_trig(IC *c, const SqmExpr *e, int en, int tn, char *out) {
    const SqmNode *x = &e->node[en], *y = &e->node[tn];
    int earg;
    if (x->kind == SQM_FUN && x->fn == SQM_FN_EXP) earg = x->a;
    else if (x->kind == SQM_POW && e->node[x->a].kind == SQM_CONST && e->node[x->a].a == 1) earg = x->b;
    else return -1;
    if (y->kind != SQM_FUN || (y->fn != SQM_FN_SIN && y->fn != SQM_FN_COS)) return -1;
    char p[T], r[T], E[T], U[T];
    if (!linear(c, e, earg, p) || !linear(c, e, y->a, r) || text_of(c, e, earg, E) || text_of(c, e, y->a, U)) return -1;
    if (y->fn == SQM_FN_SIN)
        return outn(out, T, "e^(%s)*((%s)*sin(%s) - (%s)*cos(%s))/((%s)^2 + (%s)^2)", E, p, U, r, U, p, r);
    return outn(out, T, "e^(%s)*((%s)*cos(%s) + (%s)*sin(%s))/((%s)^2 + (%s)^2)", E, p, U, r, U, p, r);
}

/* P(x) * {exp, e^, sin, cos}(a x + b), or P(x) * ln(x), by parts */
static int by_parts(IC *c, const SqmExpr *e, int pnode, int tnode, char *out) {
    static char d[MAXDERIV + 1][T]; /* not reentrant: by_parts does not recurse into itself */
    char p[T], u[T], s[T];
    const SqmNode *tn = &e->node[tnode];
    if (text_of(c, e, pnode, p)) return -1;
    int nd = poly_derivs(c, p, d);
    if (!nd || nd > MAXDERIV) return -1;
    int kind = -1; /* 0 exp, 1 sin, 2 cos, 3 ln(x) */
    int arg = -1;
    if (tn->kind == SQM_FUN && (tn->fn == SQM_FN_EXP || tn->fn == SQM_FN_SIN || tn->fn == SQM_FN_COS)) {
        kind = tn->fn == SQM_FN_EXP ? 0 : tn->fn == SQM_FN_SIN ? 1 : 2;
        arg = tn->a;
    } else if (tn->kind == SQM_POW && e->node[tn->a].kind == SQM_CONST && e->node[tn->a].a == 1) {
        kind = 0;
        arg = tn->b;
    } else if (tn->kind == SQM_FUN && tn->fn == SQM_FN_LN && e->node[tn->a].kind == SQM_VAR &&
               e->node[tn->a].a == c->vi) {
        kind = 3;
    }
    if (kind < 0) return -1;
    if (kind == 3) {
        /* Q ln x - integral of Q/x, with Q = integral of P (no constant term) */
        char Q[T], qx[2 * T];
        if (sqm_parse(p, &c->t3, NULL) != SQM_OK || integ(c, &c->t3, c->t3.root, Q)) return -1;
        outn(qx, sizeof qx, "(%s)/%s", Q, c->var);
        if (sqm_parse(qx, &c->t1, NULL) != SQM_OK || sqm_expand(&c->t1, &c->t3) != SQM_OK) return no(c, SQM_ERR_NOFORM);
        char rest[T];
        if (integ(c, &c->t3, c->t3.root, rest)) return -1;
        outn(out, T, "(%s)*ln(%s) - (%s)", Q, c->var, rest);
        return 0;
    }
    if (!linear(c, e, arg, s) || text_of(c, e, arg, u)) return -1;
    double sv = value_at(c, s, 0);
    int flip = sv < 0; /* write (P)/2^k with the sign, not (P)/(-2)^k */
    if (flip && outn(s, T, "%.17g", -sv)) return -1;
    size_t at = 0;
    if (kind == 0) {
        at += (size_t)snprintf(out + at, T - at, "e^(%s)*(0", u);
        for (int k = 0; k < nd && at < T; k++)
            at += (size_t)snprintf(out + at, T - at, " %c (%s)/(%s)^%d", (k % 2) ^ (flip && k % 2 == 0) ? '-' : '+',
                                   d[k], s, k + 1);
    } else {
        /* sin: -P cos/a + P' sin/a^2 + P'' cos/a^3 - P''' sin/a^4 ...
         * cos:  P sin/a + P' cos/a^2 - P'' sin/a^3 - P''' cos/a^4 ... */
        static const char *const SIN_F[4] = {"cos", "sin", "cos", "sin"}, *const COS_F[4] = {"sin", "cos", "sin", "cos"};
        static const char SIN_S[4] = {'-', '+', '+', '-'}, COS_S[4] = {'+', '+', '-', '-'};
        at += (size_t)snprintf(out + at, T - at, "0");
        for (int k = 0; k < nd && at < T; k++)
        {
            char sg = kind == 1 ? SIN_S[k % 4] : COS_S[k % 4];
            if (flip && k % 2 == 0) sg = sg == '+' ? '-' : '+';
            at += (size_t)snprintf(out + at, T - at, " %c (%s)*%s(%s)/(%s)^%d", sg, d[k],
                                   kind == 1 ? SIN_F[k % 4] : COS_F[k % 4], u, s, k + 1);
        }
    }
    if (at < T && kind == 0) at += (size_t)snprintf(out + at, T - at, ")");
    if (at >= T) return no(c, SQM_ERR_FULL);
    return 0;
}

/* J_k = integral of 1/q^k, q = a x^2 + b x + c with D = 4ac - b^2 > 0:
 * J_k = (2a x + b)/((k-1) D q^(k-1)) + 2a(2k-3)/((k-1) D) J_(k-1) */
static int j_text(IC *c, double a, double b, double D, const char *q, int k, char *out) {
    char ta[64], tb[64], tD[64];
    num_text(2 * a, ta, sizeof ta);
    num_text(b, tb, sizeof tb);
    num_text(D, tD, sizeof tD);
    if (k == 1) return outn(out, T, "2/sqrt%s*atan((%s*%s + %s)/sqrt%s)", tD, ta, c->var, tb, tD);
    char prev[T], f1[64], f2[64];
    if (j_text(c, a, b, D, q, k - 1, prev)) return -1;
    num_text((k - 1) * D, f1, sizeof f1);
    num_text(2 * a * (2 * k - 3) / ((k - 1) * D), f2, sizeof f2);
    return outn(out, T, "(%s*%s + %s)/(%s*(%s)^%d) + %s*(%s)", ta, c->var, tb, f1, q, k - 1, f2, prev);
}

/* (B x + C)/q^k for a quadratic q (text) with numbers, k >= 2 */
static int over_quadratic_power(IC *c, const char *num, const char *q, int k, char *out) {
    char d1[T], d2[T], d3[T], n1[T], n2[T], J[T];
    int cst;
    if (k < 2 || k > 6 || deriv_text(c, q, d1, NULL) || deriv_text(c, d1, d2, &cst) || !cst ||
        deriv_text(c, d2, d3, NULL) || !is_zero(d3) || is_zero(d2) || deriv_text(c, num, n1, NULL) ||
        deriv_text(c, n1, n2, NULL) || !is_zero(n2)) {
        c->code = SQM_OK;
        return -1;
    }
    double a = value_at(c, d2, 0) / 2, b = value_at(c, d1, 0), cc = value_at(c, q, 0);
    double B = value_at(c, n1, 0), C = value_at(c, num, 0);
    double D = 4 * a * cc - b * b;
    if (!isfinite(a + b + cc + B + C) || !(D > 0)) return -1;
    if (j_text(c, a, b, D, q, k, J)) return -1;
    char tB[64], tK[64];
    num_text(B / (2 * a), tB, sizeof tB);
    num_text(C - B * b / (2 * a), tK, sizeof tK);
    return outn(out, T, "%s*(%s)^(%d)/(%d) + %s*(%s)", tB, q, 1 - k, 1 - k, tK, J);
}

/* (B x + C)/(a x^2 + b x + c) with numbers */
static int over_quadratic(IC *c, const char *num, const char *den, char *out) {
    char d1[T], d2[T], d3[T], n1[T], n2[T];
    int k;
    if (deriv_text(c, den, d1, NULL) || deriv_text(c, d1, d2, &k) || !k || deriv_text(c, d2, d3, NULL) ||
        !is_zero(d3) || is_zero(d2) || deriv_text(c, num, n1, NULL) || deriv_text(c, n1, n2, NULL) || !is_zero(n2)) {
        c->code = SQM_OK;
        return -1;
    }
    double a = value_at(c, d2, 0) / 2, b = value_at(c, d1, 0), cc = value_at(c, den, 0);
    double B = value_at(c, n1, 0), C = value_at(c, num, 0);
    if (!isfinite(a) || !isfinite(b) || !isfinite(cc) || !isfinite(B) || !isfinite(C) || a == 0) return -1;
    double D = 4 * a * cc - b * b, K = C - B * b / (2 * a);
    if (D < 0) { /* roots: rational ones are for partial fractions */
        double r = sqrt(-D);
        char t[64];
        num_text(r, t, sizeof t);
        if (fabs(r - nearbyint(r * 1000) / 1000) < 1e-12 && !strchr(t, 'e') && strlen(t) < 20) return -1;
    }
    char ta[64], tb[64], tD[64], tB[64], tK[64], rest[T];
    num_text(2 * a, ta, sizeof ta);
    num_text(b, tb, sizeof tb);
    num_text(fabs(D), tD, sizeof tD);
    num_text(B / (2 * a), tB, sizeof tB);
    num_text(K, tK, sizeof tK);
    if (K == 0) outn(rest, sizeof rest, "0");
    else if (D > 0)
        outn(rest, sizeof rest, "%s*2/sqrt%s*atan((%s*%s + %s)/sqrt%s)", tK, tD, ta, c->var, tb, tD);
    else if (D < 0)
        outn(rest, sizeof rest, "%s/sqrt%s*ln(abs((%s*%s + %s - sqrt%s)/(%s*%s + %s + sqrt%s)))", tK, tD, ta, c->var,
                 tb, tD, ta, c->var, tb, tD);
    else outn(rest, sizeof rest, "-%s*2/(%s*%s + %s)", tK, ta, c->var, tb);
    outn(out, T, "%s*ln(abs(%s)) + %s", tB, den, rest);
    return 0;
}

/* the integral of text (parsed into a copy kept for this depth) */
static int integ_text(IC *c, const char *text, char *out) {
    static SqmExpr hold[MAXDEPTH];
    if (sqm_parse(text, &hold[c->depth], NULL) != SQM_OK) return no(c, SQM_ERR_FULL);
    return integ(c, &hold[c->depth], hold[c->depth].root, out);
}

/* the expanded form, or partial fractions, when they are different */
static int rewrite(IC *c, const SqmExpr *e, int i, int apart, char *out) {
    char before[T], after[T];
    if (copy(c, &c->t1, e, i) || print(c, &c->t1, before)) return -1;
    int rc = apart ? sqm_apart(&c->t1, c->var, &c->t2) : sqm_expand(&c->t1, &c->t2);
    if (rc != SQM_OK) {
        c->code = SQM_OK;
        return -1;
    }
    if (print(c, &c->t2, after) || !strcmp(before, after)) {
        c->code = SQM_OK;
        return -1;
    }
    return integ_text(c, after, out);
}

static int integ2(IC *c, const SqmExpr *e, int i, char *out) {
    const SqmNode *nd = &e->node[i];
    char a[T], b[T], s[T], u[T];
    if (!uses(c, e, i)) { /* a constant */
        if (text_of(c, e, i, u)) return -1;
        outn(out, T, "(%s)*%s", u, c->var);
        return 0;
    }
    switch (nd->kind) {
        case SQM_VAR: outn(out, T, "%s^2/2", c->var); return 0;
        case SQM_NEG:
            if (integ(c, e, nd->a, a)) return -1;
            outn(out, T, "-(%s)", a);
            return 0;
        case SQM_ADD:
        case SQM_SUB:
            if (integ(c, e, nd->a, a) || integ(c, e, nd->b, b)) return -1;
            outn(out, T, "(%s) %c (%s)", a, nd->kind == SQM_ADD ? '+' : '-', b);
            return 0;
        case SQM_MUL:
            if (!uses(c, e, nd->a) || !uses(c, e, nd->b)) {
                int k = uses(c, e, nd->a) ? nd->b : nd->a, f = k == nd->a ? nd->b : nd->a;
                if (text_of(c, e, k, u) || integ(c, e, f, a)) return -1;
                outn(out, T, "(%s)*(%s)", u, a);
                return 0;
            }
            if (text_of(c, e, nd->a, a) || text_of(c, e, nd->b, b)) return -1;
            if (substitution(c, e, nd->a, b, out) == 0 || substitution(c, e, nd->b, a, out) == 0) return 0;
            if (exp_trig(c, e, nd->a, nd->b, out) == 0 || exp_trig(c, e, nd->b, nd->a, out) == 0) return 0;
            c->code = SQM_OK;
            if (by_parts(c, e, nd->a, nd->b, out) == 0 || by_parts(c, e, nd->b, nd->a, out) == 0) return 0;
            c->code = SQM_OK;
            break;
        case SQM_DIV:
            if (!uses(c, e, nd->b)) {
                if (text_of(c, e, nd->b, u) || integ(c, e, nd->a, a)) return -1;
                outn(out, T, "(%s)/(%s)", a, u);
                return 0;
            }
            if (text_of(c, e, nd->a, a) || text_of(c, e, nd->b, b)) return -1;
            {
                const SqmNode *bn = &e->node[nd->b];
                /* a/(k g): (1/k) times the integral of a/g */
                if (bn->kind == SQM_MUL && (!uses(c, e, bn->a) || !uses(c, e, bn->b))) {
                    int kn = uses(c, e, bn->a) ? bn->b : bn->a, gn = kn == bn->a ? bn->b : bn->a;
                    char kt[T], gt[T], q[2 * T + 16], r[T];
                    if (text_of(c, e, kn, kt) || text_of(c, e, gn, gt) || outn(q, sizeof q, "(%s)/(%s)", a, gt)) return -1;
                    if (integ_text(c, q, r) == 0) return outn(out, T, "(%s)/(%s)", r, kt);
                    c->code = SQM_OK;
                }
                /* k / sqrt(g), k / cbrt(g), g linear */
                if (!uses(c, e, nd->a) && bn->kind == SQM_FUN && (bn->fn == SQM_FN_SQRT || bn->fn == SQM_FN_CBRT) &&
                    linear(c, e, bn->a, s)) {
                    if (text_of(c, e, bn->a, u)) return -1;
                    if (bn->fn == SQM_FN_SQRT) return outn(out, T, "2*(%s)*sqrt(%s)/(%s)", a, u, s);
                    return outn(out, T, "3*(%s)*(%s)^(2/3)/(2*(%s))", a, u, s);
                }
            }
            {
                char db[T], k[T];
                /* g'/g: ln|g| */
                if (deriv_text(c, b, db, NULL) == 0 && !is_zero(db) && ratio_const(c, a, db, k)) {
                    outn(out, T, "(%s)*ln(abs(%s))", k, b);
                    return 0;
                }
                c->code = SQM_OK;
                /* k / g^n with g linear */
                if (!uses(c, e, nd->a) && e->node[nd->b].kind == SQM_POW && !uses(c, e, e->node[nd->b].b) &&
                    linear(c, e, e->node[nd->b].a, s)) {
                    char g[T], n[T];
                    if (text_of(c, e, e->node[nd->b].a, g) || text_of(c, e, e->node[nd->b].b, n)) return -1;
                    if (value_at(c, n, 0) == 1) outn(out, T, "(%s)*ln(abs(%s))/(%s)", a, g, s);
                    else outn(out, T, "(%s)*(%s)^(1 - (%s))/((1 - (%s))*(%s))", a, g, n, n, s);
                    return 0;
                }
                if (over_quadratic(c, a, b, out) == 0) return 0;
                c->code = SQM_OK;
                {
                    const SqmNode *bn = &e->node[nd->b];
                    char q[T], n[T];
                    if (bn->kind == SQM_POW && !uses(c, e, bn->b) && text_of(c, e, bn->a, q) == 0 &&
                        text_of(c, e, bn->b, n) == 0) {
                        double kv = value_at(c, n, 0);
                        if (kv == nearbyint(kv) && fabs(kv) < 100 && over_quadratic_power(c, a, q, (int)kv, out) == 0)
                            return 0;
                    }
                    c->code = SQM_OK;
                }
                /* f(g) / h, with 1/h = k g' */
                char inv[2 * T + 8];
                if (outn(inv, sizeof inv, "1/(%s)", b) == 0 && substitution(c, e, nd->a, inv, out) == 0) return 0;
                c->code = SQM_OK;
                /* a / g^n and a / sqrt(g), with a = k g' */
                const SqmNode *bn = &e->node[nd->b];
                char F[T], g[T], n[T];
                if (bn->kind == SQM_POW && !uses(c, e, bn->b) && text_of(c, e, bn->a, g) == 0 &&
                    text_of(c, e, bn->b, n) == 0 && value_at(c, n, 0) != 1 &&
                    outn(F, T, "(%s)^(1 - (%s))/(1 - (%s))", g, n, n) == 0 && sub_core(c, F, g, a, out) == 0)
                    return 0;
                c->code = SQM_OK;
                if (bn->kind == SQM_FUN && bn->fn == SQM_FN_SQRT && text_of(c, e, bn->a, g) == 0 &&
                    outn(F, T, "2*sqrt(%s)", g) == 0 && sub_core(c, F, g, a, out) == 0)
                    return 0;
                c->code = SQM_OK;
            }
            if (rewrite(c, e, i, 1, out) == 0) return 0;
            c->code = SQM_OK;
            break;
        case SQM_POW: {
            if (!uses(c, e, nd->b) && linear(c, e, nd->a, s)) { /* (a x + b)^n */
                char n[T];
                if (text_of(c, e, nd->a, u) || text_of(c, e, nd->b, n)) return -1;
                if (value_at(c, n, 0) == -1) outn(out, T, "ln(abs(%s))/(%s)", u, s);
                else outn(out, T, "(%s)^((%s) + 1)/(((%s) + 1)*(%s))", u, n, n, s);
                return 0;
            }
            if (!uses(c, e, nd->a) && linear(c, e, nd->b, s)) { /* b^(a x + c) */
                int inner;
                char F[T];
                if (outer(c, e, i, &inner, F)) return -1;
                outn(out, T, "(%s)/(%s)", F, s);
                return 0;
            }
            {
                const SqmNode *bn = &e->node[nd->a];
                char n[T];
                if (bn->kind == SQM_FUN && (bn->fn == SQM_FN_SIN || bn->fn == SQM_FN_COS) && linear(c, e, bn->a, s) &&
                    text_of(c, e, nd->b, n) == 0 && value_at(c, n, 0) == 2) {
                    if (text_of(c, e, bn->a, u)) return -1;
                    /* sin^2 u = u/2 - sin(2u)/4 (and + for cos), over the slope */
                    return outn(out, T, "((%s)/2 %c sin(2*(%s))/4)/(%s)", u, bn->fn == SQM_FN_SIN ? '-' : '+', u, s);
                }
            }
            if (substitution(c, e, i, "1", out) == 0) return 0;
            break;
        }
        case SQM_FUN:
            if (sqm_fn_args(nd->fn) == 1 && linear(c, e, nd->a, s)) {
                if (text_of(c, e, nd->a, u)) return -1;
                if (table(nd->fn, u, a) == 0) {
                    outn(out, T, "(%s)/(%s)", a, s);
                    return 0;
                }
            }
            break;
        default: break;
    }
    c->code = SQM_OK;
    if (rewrite(c, e, i, 0, out) == 0) return 0;
    return no(c, SQM_ERR_NOFORM);
}

static int integ(IC *c, const SqmExpr *e, int i, char *out) {
    if (c->depth >= MAXDEPTH - 1) return no(c, SQM_ERR_DEPTH);
    c->depth++;
    int r = integ2(c, e, i, out);
    c->depth--;
    if (r == 0 && c->code) c->code = SQM_OK; /* a rule that failed on the way, then another that worked */
    return r;
}

int sqm_antideriv(const SqmExpr *in, const char *var, SqmExpr *out) {
    static IC c; /* about 40 KB: kept off the stack */
    char text[T];
    if (!in || !var || in == out || in->root < 0) return SQM_ERR_SYNTAX;
    memset(&c, 0, sizeof c);
    c.in = in;
    c.var = var;
    c.vi = sqm__find_name(in, var);
    if (integ(&c, in, in->root, text)) {
        int code = c.code ? c.code : SQM_ERR_NOFORM;
        sqm__clear(out);
        return code;
    }
    if (sqm_parse(text, &c.t3, NULL) != SQM_OK) {
        sqm__clear(out);
        return SQM_ERR_FULL;
    }
    int rc = sqm_simplify(&c.t3, out);
    if (rc) {
        sqm__clear(out);
        return rc;
    }
    /* the expanded form when it is shorter: 3*x^3/3 is x^3 */
    char a[T], b[T];
    if (sqm_print(out, a, sizeof a) < sizeof a && sqm_expand(out, &c.t1) == SQM_OK &&
        sqm_print(&c.t1, b, sizeof b) < strlen(a))
        *out = c.t1;
    return SQM_OK;
}
