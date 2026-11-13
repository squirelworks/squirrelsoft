/* sqmath: simplification and derivatives (see sqmath.h). Original code.
 *
 * Both rebuild the tree into a second SqmExpr through "smart constructors"
 * (add, mul, pow, ...) that fold numbers and apply identities as each node is
 * made, so a derivative comes out simplified without a separate pass. */
#include <math.h>
#include <string.h>

#include "sqmath.h"
#include "sqmath_internal.h"

typedef struct {
    const SqmExpr *in;
    SqmExpr *out;
    int var;  /* name index being differentiated, or -1 */
    int code; /* first error */
} Sym;

#define ND(s, i) (&(s)->out->node[i])

static int node(Sym *s, int kind, int fn, int a, int b, double v) {
    if (s->code) return -1;
    int i = sqm__node(s->out, kind, fn, a, b, v);
    if (i < 0) s->code = SQM_ERR_FULL;
    return i;
}

static int num(Sym *s, double v) { return node(s, SQM_NUM, 0, -1, -1, v); }

static int is_num(Sym *s, int i) { return i >= 0 && ND(s, i)->kind == SQM_NUM; }
static int is_val(Sym *s, int i, double v) { return is_num(s, i) && ND(s, i)->v == v; }

/* a whole number that doubles hold exactly */
static int exact_int(double v) { return isfinite(v) && v == floor(v) && fabs(v) < 1e15; }

/* structurally equal subtrees of out */
static int same(Sym *s, int a, int b) {
    if (a == b) return 1;
    const SqmNode *x = ND(s, a), *y = ND(s, b);
    if (x->kind != y->kind) return 0;
    switch (x->kind) {
        case SQM_NUM: return x->v == y->v;
        case SQM_CONST:
        case SQM_VAR: return x->a == y->a;
        case SQM_NEG: return same(s, x->a, y->a);
        case SQM_FUN:
            if (x->fn != y->fn || !same(s, x->a, y->a)) return 0;
            return sqm_fn_args(x->fn) == 1 || same(s, x->b, y->b);
        default: return same(s, x->a, y->a) && same(s, x->b, y->b);
    }
}

static int neg(Sym *s, int a);
static int add(Sym *s, int a, int b);
static int sub(Sym *s, int a, int b);
static int mul(Sym *s, int a, int b);
static int dv(Sym *s, int a, int b);
static int pw(Sym *s, int a, int b);

/* split k*rest (k a number): -x is -1*x, plain x is 1*x */
static void coeff(Sym *s, int i, double *k, int *rest) {
    const SqmNode *n = ND(s, i);
    if (n->kind == SQM_MUL && is_num(s, n->a)) {
        *k = ND(s, n->a)->v;
        *rest = n->b;
    } else if (n->kind == SQM_NEG) {
        coeff(s, n->a, k, rest);
        *k = -*k;
    } else {
        *k = 1;
        *rest = i;
    }
}

static int scaled(Sym *s, double k, int rest) { return mul(s, num(s, k), rest); }

static int neg(Sym *s, int a) {
    if (a < 0) return -1;
    const SqmNode *n = ND(s, a);
    if (n->kind == SQM_NUM) return num(s, -n->v);
    if (n->kind == SQM_NEG) return n->a;
    if (n->kind == SQM_MUL && is_num(s, n->a)) return scaled(s, -ND(s, n->a)->v, n->b);
    return node(s, SQM_NEG, 0, a, -1, 0);
}

static int add(Sym *s, int a, int b) {
    if (a < 0 || b < 0) return -1;
    if (is_num(s, a) && is_num(s, b)) return num(s, ND(s, a)->v + ND(s, b)->v);
    if (is_val(s, a, 0)) return b;
    if (is_val(s, b, 0)) return a;
    if (ND(s, b)->kind == SQM_NEG) return sub(s, a, ND(s, b)->a);
    if (is_num(s, b) && ND(s, b)->v < 0) return sub(s, a, num(s, -ND(s, b)->v));
    if (ND(s, a)->kind == SQM_NEG) return sub(s, b, ND(s, a)->a);
    double ka, kb;
    int ra, rb;
    coeff(s, a, &ka, &ra);
    coeff(s, b, &kb, &rb);
    if (!is_num(s, ra) && same(s, ra, rb)) return scaled(s, ka + kb, ra);
    if (kb < 0 && !is_num(s, rb)) return sub(s, a, scaled(s, -kb, rb)); /* a + -2y = a - 2y */
    const SqmNode *y = ND(s, b); /* a + (p ± q) = (a + p) ± q: chains lean left, so terms meet */
    if (y->kind == SQM_ADD) return add(s, add(s, a, y->a), y->b);
    if (y->kind == SQM_SUB) return sub(s, add(s, a, y->a), y->b);
    const SqmNode *x = ND(s, a);
    if (x->kind == SQM_SUB && same(s, x->b, b)) return x->a; /* (p - q) + q */
    if (x->kind == SQM_ADD || x->kind == SQM_SUB) {           /* (p ± q) + b with q, b alike */
        double kq;
        int rq;
        coeff(s, x->b, &kq, &rq);
        if (!is_num(s, rq) && same(s, rq, rb))
            return add(s, x->a, scaled(s, (x->kind == SQM_ADD ? kq : -kq) + kb, rq));
        double kp;
        int rp;
        coeff(s, x->a, &kp, &rp); /* (p ± q) + b with p, b alike */
        if (!is_num(s, rp) && same(s, rp, rb)) {
            int sum = scaled(s, kp + kb, rp);
            return x->kind == SQM_ADD ? add(s, sum, x->b) : sub(s, sum, x->b);
        }
    }
    return node(s, SQM_ADD, 0, a, b, 0);
}

static int sub(Sym *s, int a, int b) {
    if (a < 0 || b < 0) return -1;
    if (is_num(s, a) && is_num(s, b)) return num(s, ND(s, a)->v - ND(s, b)->v);
    if (is_val(s, b, 0)) return a;
    if (is_val(s, a, 0)) return neg(s, b);
    if (same(s, a, b)) return num(s, 0);
    if (ND(s, b)->kind == SQM_NEG) return add(s, a, ND(s, b)->a);
    if (is_num(s, b) && ND(s, b)->v < 0) return add(s, a, num(s, -ND(s, b)->v));
    double ka, kb;
    int ra, rb;
    coeff(s, a, &ka, &ra);
    coeff(s, b, &kb, &rb);
    if (!is_num(s, ra) && same(s, ra, rb)) return scaled(s, ka - kb, ra);
    if (kb < 0 && !is_num(s, rb)) return add(s, a, scaled(s, -kb, rb)); /* a - -2y = a + 2y */
    const SqmNode *y = ND(s, b); /* a - (p ± q) = (a - p) ∓ q */
    if (y->kind == SQM_ADD) return sub(s, sub(s, a, y->a), y->b);
    if (y->kind == SQM_SUB) return add(s, sub(s, a, y->a), y->b);
    const SqmNode *x = ND(s, a);
    if (x->kind == SQM_ADD && same(s, x->b, b)) return x->a; /* (p + q) - q */
    if (x->kind == SQM_ADD && same(s, x->a, b)) return x->b; /* (p + q) - p */
    if (x->kind == SQM_ADD || x->kind == SQM_SUB) {          /* (p ± q) - b with q, b alike */
        double kq;
        int rq;
        coeff(s, x->b, &kq, &rq);
        if (!is_num(s, rq) && same(s, rq, rb))
            return add(s, x->a, scaled(s, (x->kind == SQM_ADD ? kq : -kq) - kb, rq));
        double kp;
        int rp;
        coeff(s, x->a, &kp, &rp); /* (p ± q) - b with p, b alike */
        if (!is_num(s, rp) && same(s, rp, rb)) {
            int diff = scaled(s, kp - kb, rp);
            return x->kind == SQM_ADD ? add(s, diff, x->b) : sub(s, diff, x->b);
        }
    }
    return node(s, SQM_SUB, 0, a, b, 0);
}

static int mul(Sym *s, int a, int b) {
    if (a < 0 || b < 0) return -1;
    if (is_num(s, a) && is_num(s, b)) return num(s, ND(s, a)->v * ND(s, b)->v);
    if (is_val(s, a, 0) || is_val(s, b, 0)) return num(s, 0);
    if (is_val(s, a, 1)) return b;
    if (is_val(s, b, 1)) return a;
    if (is_val(s, a, -1)) return neg(s, b);
    if (is_val(s, b, -1)) return neg(s, a);
    if (is_num(s, b)) return mul(s, b, a); /* numbers first: 2*x */
    const SqmNode *x = ND(s, a), *y = ND(s, b);
    if (is_num(s, a) && y->kind == SQM_MUL && is_num(s, y->a)) return scaled(s, x->v * ND(s, y->a)->v, y->b);
    if (x->kind == SQM_NEG) return neg(s, mul(s, x->a, b));
    if (y->kind == SQM_NEG) return neg(s, mul(s, a, y->a));
    if (!is_num(s, a) && y->kind == SQM_MUL && is_num(s, y->a)) /* x*(2*y) = 2*(x*y) */
        return scaled(s, ND(s, y->a)->v, mul(s, a, y->b));
    if (x->kind == SQM_MUL && is_num(s, x->a)) return scaled(s, ND(s, x->a)->v, mul(s, x->b, b));
    if (x->kind == SQM_DIV) return dv(s, mul(s, x->a, b), x->b);
    if (y->kind == SQM_DIV) return dv(s, mul(s, a, y->a), y->b);
    if (same(s, a, b)) return pw(s, a, num(s, 2));
    if (x->kind == SQM_POW && same(s, x->a, b)) return pw(s, b, add(s, x->b, num(s, 1)));
    if (y->kind == SQM_POW && same(s, y->a, a)) return pw(s, a, add(s, y->b, num(s, 1)));
    if (x->kind == SQM_POW && y->kind == SQM_POW && same(s, x->a, y->a)) return pw(s, x->a, add(s, x->b, y->b));
    return node(s, SQM_MUL, 0, a, b, 0);
}

static double gcd(double a, double b) {
    a = fabs(a);
    b = fabs(b);
    while (b > 0) {
        double t = fmod(a, b);
        a = b;
        b = t;
    }
    return a;
}

static int dv(Sym *s, int a, int b) {
    if (a < 0 || b < 0) return -1;
    if (is_num(s, a) && is_num(s, b) && ND(s, b)->v != 0) {
        double p = ND(s, a)->v, q = ND(s, b)->v;
        if (exact_int(p / q) && p / q * q == p) return num(s, p / q);
        if (exact_int(p) && exact_int(q)) { /* reduce: 6/4 = 3/2, 2/-4 = -1/2 */
            double g = gcd(p, q);
            if (q < 0) g = -g;
            if (g != 1) return node(s, SQM_DIV, 0, num(s, p / g), num(s, q / g), 0);
        }
    }
    if (is_val(s, a, 0) && !is_val(s, b, 0)) return num(s, 0);
    {
        /* common whole factors of the coefficients: 2x/(4y) = x/(2y) */
        double ka, kb;
        int ra, rb;
        coeff(s, a, &ka, &ra);
        coeff(s, b, &kb, &rb);
        if (!is_num(s, ra) && !is_num(s, rb) && exact_int(ka) && exact_int(kb) && kb != 0) {
            double g = gcd(ka, kb);
            if (kb < 0) g = -g;
            if (g != 1) return dv(s, scaled(s, ka / g, ra), scaled(s, kb / g, rb));
        }
    }
    if (is_val(s, b, 1)) return a;
    if (is_val(s, b, -1)) return neg(s, a);
    if (same(s, a, b) && !is_val(s, a, 0)) return num(s, 1);
    const SqmNode *x = ND(s, a), *y = ND(s, b);
    if (x->kind == SQM_NEG) return neg(s, dv(s, x->a, b));
    if (y->kind == SQM_NEG) return neg(s, dv(s, a, y->a));
    if (x->kind == SQM_DIV) return dv(s, x->a, mul(s, x->b, b)); /* (p/q)/r = p/(q*r) */
    if (y->kind == SQM_DIV) return dv(s, mul(s, a, y->b), y->a); /* p/(q/r) = p*r/q */
    if (x->kind == SQM_POW && same(s, x->a, b)) return pw(s, b, sub(s, x->b, num(s, 1)));
    return node(s, SQM_DIV, 0, a, b, 0);
}

static int pw(Sym *s, int a, int b) {
    if (a < 0 || b < 0) return -1;
    if (is_num(s, a) && is_num(s, b)) {
        double r = pow(ND(s, a)->v, ND(s, b)->v);
        if (exact_int(r)) return num(s, r);
    }
    if (is_val(s, b, 0)) return num(s, 1);
    if (is_val(s, b, 1)) return a;
    if (is_val(s, a, 1)) return num(s, 1);
    if (is_val(s, a, 0) && is_num(s, b) && ND(s, b)->v > 0) return num(s, 0);
    const SqmNode *x = ND(s, a);
    /* (u^m)^n = u^(m*n) for whole m and n */
    if (x->kind == SQM_POW && is_num(s, x->b) && is_num(s, b) && exact_int(ND(s, x->b)->v) &&
        exact_int(ND(s, b)->v))
        return pw(s, x->a, num(s, ND(s, x->b)->v * ND(s, b)->v));
    return node(s, SQM_POW, 0, a, b, 0);
}

static int fun(Sym *s, int fn, int a, int b) {
    if (a < 0 || (sqm_fn_args(fn) == 2 && b < 0)) return -1;
    if (is_num(s, a) && (sqm_fn_args(fn) == 1 || is_num(s, b))) {
        double r = sqm__apply(fn, ND(s, a)->v, sqm_fn_args(fn) == 2 ? ND(s, b)->v : 0);
        if (exact_int(r)) return num(s, r); /* sqrt(4), abs(-3), exp(0); sqrt(2) stays */
    }
    const SqmNode *x = ND(s, a);
    if (fn == SQM_FN_LN && x->kind == SQM_CONST && x->a == 1) return num(s, 1);   /* ln(e) */
    if (fn == SQM_FN_LN && x->kind == SQM_FUN && x->fn == SQM_FN_EXP) return x->a; /* ln(exp(u)) */
    return node(s, SQM_FUN, fn, a, sqm_fn_args(fn) == 2 ? b : -1, 0);
}

static int fun1(Sym *s, int fn, int a) { return fun(s, fn, a, -1); }

/* in -> out, simplified */
static int copy(Sym *s, int i) {
    if (s->code) return -1;
    const SqmNode *n = &s->in->node[i];
    switch (n->kind) {
        case SQM_NUM: return num(s, n->v);
        case SQM_CONST:
        case SQM_VAR: return node(s, n->kind, 0, n->a, -1, n->v);
        case SQM_NEG: return neg(s, copy(s, n->a));
        case SQM_ADD: return add(s, copy(s, n->a), copy(s, n->b));
        case SQM_SUB: return sub(s, copy(s, n->a), copy(s, n->b));
        case SQM_MUL: return mul(s, copy(s, n->a), copy(s, n->b));
        case SQM_DIV: return dv(s, copy(s, n->a), copy(s, n->b));
        case SQM_POW: return pw(s, copy(s, n->a), copy(s, n->b));
        case SQM_FUN: return fun(s, n->fn, copy(s, n->a), sqm_fn_args(n->fn) == 2 ? copy(s, n->b) : -1);
        default: s->code = SQM_ERR_SYNTAX; return -1;
    }
}

static int uses(Sym *s, int i) { return sqm__uses_index(s->in, i, s->var); }

/* d/dvar of in node i, into out */
static int diff(Sym *s, int i) {
    if (s->code) return -1;
    if (!uses(s, i)) return num(s, 0);
    const SqmNode *n = &s->in->node[i];
    int u = n->a, w = n->b;
    switch (n->kind) {
        case SQM_VAR: return num(s, 1);
        case SQM_NEG: return neg(s, diff(s, u));
        case SQM_ADD: return add(s, diff(s, u), diff(s, w));
        case SQM_SUB: return sub(s, diff(s, u), diff(s, w));
        case SQM_MUL: return add(s, mul(s, diff(s, u), copy(s, w)), mul(s, copy(s, u), diff(s, w)));
        case SQM_DIV:
            if (!uses(s, w)) return dv(s, diff(s, u), copy(s, w));
            return dv(s, sub(s, mul(s, diff(s, u), copy(s, w)), mul(s, copy(s, u), diff(s, w))),
                      pw(s, copy(s, w), num(s, 2)));
        case SQM_POW:
            if (!uses(s, w)) /* w * u^(w-1) * u' */
                return mul(s, mul(s, copy(s, w), pw(s, copy(s, u), sub(s, copy(s, w), num(s, 1)))), diff(s, u));
            if (!uses(s, u)) /* u^w * ln(u) * w' */
                return mul(s, mul(s, pw(s, copy(s, u), copy(s, w)), fun1(s, SQM_FN_LN, copy(s, u))), diff(s, w));
            /* u^w * (w' ln u + w u'/u) */
            return mul(s, pw(s, copy(s, u), copy(s, w)),
                       add(s, mul(s, diff(s, w), fun1(s, SQM_FN_LN, copy(s, u))),
                           dv(s, mul(s, copy(s, w), diff(s, u)), copy(s, u))));
        case SQM_FUN: break;
        default: s->code = SQM_ERR_SYNTAX; return -1;
    }
    int g; /* the outer derivative, at u */
    switch (n->fn) {
        case SQM_FN_SIN: g = fun1(s, SQM_FN_COS, copy(s, u)); break;
        case SQM_FN_COS: g = neg(s, fun1(s, SQM_FN_SIN, copy(s, u))); break;
        case SQM_FN_TAN: g = dv(s, num(s, 1), pw(s, fun1(s, SQM_FN_COS, copy(s, u)), num(s, 2))); break;
        case SQM_FN_ASIN:
            g = dv(s, num(s, 1), fun1(s, SQM_FN_SQRT, sub(s, num(s, 1), pw(s, copy(s, u), num(s, 2)))));
            break;
        case SQM_FN_ACOS:
            g = neg(s, dv(s, num(s, 1), fun1(s, SQM_FN_SQRT, sub(s, num(s, 1), pw(s, copy(s, u), num(s, 2))))));
            break;
        case SQM_FN_ATAN: g = dv(s, num(s, 1), add(s, num(s, 1), pw(s, copy(s, u), num(s, 2)))); break;
        case SQM_FN_SINH: g = fun1(s, SQM_FN_COSH, copy(s, u)); break;
        case SQM_FN_COSH: g = fun1(s, SQM_FN_SINH, copy(s, u)); break;
        case SQM_FN_TANH: g = dv(s, num(s, 1), pw(s, fun1(s, SQM_FN_COSH, copy(s, u)), num(s, 2))); break;
        case SQM_FN_EXP: g = fun1(s, SQM_FN_EXP, copy(s, u)); break;
        case SQM_FN_LN: g = dv(s, num(s, 1), copy(s, u)); break;
        case SQM_FN_LOG: g = dv(s, num(s, 1), mul(s, copy(s, u), fun1(s, SQM_FN_LN, num(s, 10)))); break;
        case SQM_FN_SQRT: g = dv(s, num(s, 1), mul(s, num(s, 2), fun1(s, SQM_FN_SQRT, copy(s, u)))); break;
        case SQM_FN_CBRT:
            g = dv(s, num(s, 1), mul(s, num(s, 3), pw(s, fun1(s, SQM_FN_CBRT, copy(s, u)), num(s, 2))));
            break;
        case SQM_FN_ABS: g = fun1(s, SQM_FN_SIGN, copy(s, u)); break;
        case SQM_FN_FLOOR:
        case SQM_FN_CEIL:
        case SQM_FN_ROUND:
        case SQM_FN_SIGN: return num(s, 0); /* zero wherever it exists */
        case SQM_FN_ATAN2: {
            /* atan2(y, x)' = (x y' - y x') / (x^2 + y^2) */
            int top = sub(s, mul(s, copy(s, w), diff(s, u)), mul(s, copy(s, u), diff(s, w)));
            return dv(s, top, add(s, pw(s, copy(s, w), num(s, 2)), pw(s, copy(s, u), num(s, 2))));
        }
        case SQM_FN_HYPOT: {
            int top = add(s, mul(s, copy(s, u), diff(s, u)), mul(s, copy(s, w), diff(s, w)));
            return dv(s, top, fun(s, SQM_FN_HYPOT, copy(s, u), copy(s, w)));
        }
        default: s->code = SQM_ERR_DIFF; return -1; /* fact, min, max, mod */
    }
    return mul(s, g, diff(s, u));
}

static void start(Sym *s, const SqmExpr *in, SqmExpr *out, int var) {
    s->in = in;
    s->out = out;
    s->var = var;
    s->code = SQM_OK;
    sqm__clear(out);
    out->nnames = in->nnames;
    memcpy(out->names, in->names, sizeof out->names);
}

static int finish(Sym *s, int root) {
    if (s->code || root < 0) {
        int code = s->code ? s->code : SQM_ERR_SYNTAX;
        sqm__clear(s->out);
        return code;
    }
    s->out->root = root;
    return SQM_OK;
}

int sqm_simplify(const SqmExpr *in, SqmExpr *out) {
    Sym s;
    if (in == out || in->root < 0) return SQM_ERR_SYNTAX;
    start(&s, in, out, -1);
    return finish(&s, copy(&s, in->root));
}

int sqm_diff(const SqmExpr *in, const char *var, SqmExpr *out) {
    Sym s;
    if (in == out || in->root < 0 || !var) return SQM_ERR_SYNTAX;
    start(&s, in, out, sqm__find_name(in, var));
    return finish(&s, diff(&s, in->root));
}
