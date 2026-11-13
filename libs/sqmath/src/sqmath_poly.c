/* sqmath: expansion, collection and exact equation solving (see sqmath.h).
 * Original code.
 *
 * An expression is expanded into a sum of terms c * a1^k1 * a2^k2 ..., where
 * c is an exact fraction and each ai is an "atom": a variable, pi or e, or any
 * part that is not a polynomial (sin(x + 1), sqrt(2), 1/(x - 1) is the atom
 * x - 1 to the power -1). Atoms are kept as canonical text (parsed,
 * simplified and printed), so equal parts compare equal. The result is
 * written back as text and parsed into the output expression. */
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqmath.h"
#include "sqmath_internal.h"

#define PT 64       /* terms in a polynomial */
#define PA 8        /* atoms in one expansion */
#define ATOM_LEN 96 /* canonical text of an atom */
#define TEXT 2048   /* the whole result as text */
#define MAXPOW 64   /* largest whole power expanded */
#define MAXEXP 120  /* largest exponent of an atom in a term */
#define MAXDEPTH 48 /* nesting expanded (about 2 KB of stack each) */
#define LIM ((int64_t)1 << 62)

typedef struct {
    int64_t p, q; /* q > 0, gcd(p, q) = 1 */
} Rat;

typedef struct {
    Rat c;
    int8_t e[PA];
} Term;

typedef struct {
    int n;
    Term t[PT];
} Poly;

typedef struct {
    const SqmExpr *in;
    int natoms;
    char atom[PA][ATOM_LEN];
    int code;
    int depth;
    SqmExpr tmp, tmp2;
    Poly prod; /* product scratch: never live across a recursive call */
} Ctx;

static void fail(Ctx *c, int code) {
    if (!c->code) c->code = code;
}

/* ---- exact fractions ---- */

static int64_t gcd64(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static uint64_t mag(int64_t a) { return a < 0 ? (uint64_t)0 - (uint64_t)a : (uint64_t)a; }

/* a*b with |result| < 2^62, else -1 */
static int mul64(int64_t a, int64_t b, int64_t *r) {
    if (a == 0 || b == 0) {
        *r = 0;
        return 0;
    }
    if (mag(a) >= (uint64_t)LIM || mag(b) >= (uint64_t)LIM || mag(a) > (uint64_t)LIM / mag(b)) return -1;
    *r = a * b;
    return 0;
}

static int add64(int64_t a, int64_t b, int64_t *r) {
    if (mag(a) >= (uint64_t)LIM || mag(b) >= (uint64_t)LIM) return -1;
    int64_t s = a + b; /* both below 2^62: no overflow */
    if (mag(s) >= (uint64_t)LIM) return -1;
    *r = s;
    return 0;
}

static Rat rat(int64_t p, int64_t q) {
    if (q < 0) {
        p = -p;
        q = -q;
    }
    int64_t g = gcd64(p, q);
    if (g > 1) {
        p /= g;
        q /= g;
    }
    if (p == 0) q = 1;
    Rat r = {p, q};
    return r;
}

static int rat_add(Ctx *c, Rat a, Rat b, Rat *r) {
    int64_t g = gcd64(a.q, b.q), x, y, q;
    if (mul64(a.p, b.q / g, &x) || mul64(b.p, a.q / g, &y) || add64(x, y, &x) || mul64(a.q / g, b.q, &q)) {
        fail(c, SQM_ERR_RANGE);
        return -1;
    }
    *r = rat(x, q);
    return 0;
}

static int rat_mul(Ctx *c, Rat a, Rat b, Rat *r) {
    int64_t g1 = gcd64(a.p, b.q), g2 = gcd64(b.p, a.q), p, q;
    if (g1 == 0) g1 = 1;
    if (g2 == 0) g2 = 1;
    if (mul64(a.p / g1, b.p / g2, &p) || mul64(a.q / g2, b.q / g1, &q)) {
        fail(c, SQM_ERR_RANGE);
        return -1;
    }
    *r = rat(p, q);
    return 0;
}

static int rat_pow(Ctx *c, Rat a, int k, Rat *r) {
    Rat x = {1, 1};
    if (k < 0) {
        if (a.p == 0) {
            fail(c, SQM_ERR_RANGE);
            return -1;
        }
        a = rat(a.q, a.p);
        k = -k;
    }
    while (k-- > 0)
        if (rat_mul(c, x, a, &x)) return -1;
    *r = x;
    return 0;
}

/* an exact fraction for a double, if it is a short decimal or a whole number */
static int rat_from(double x, Rat *r) {
    if (!isfinite(x)) return -1;
    int64_t den = 1;
    for (int k = 0; k <= 9; k++, den *= 10) {
        double s = x * (double)den;
        if (fabs(s) >= 4e15) return -1;
        double n = nearbyint(s);
        if (fabs(s - n) <= 4 * DBL_EPSILON * fabs(n)) {
            *r = rat((int64_t)n, den);
            return 0;
        }
    }
    return -1;
}

static double rat_val(Rat a) { return (double)a.p / (double)a.q; }

static int rat_text(Rat a, char *out, size_t cap) {
    if (a.q == 1) return snprintf(out, cap, "%lld", (long long)a.p);
    return snprintf(out, cap, "%lld/%lld", (long long)a.p, (long long)a.q);
}

/* ---- polynomials ---- */

static void poly_const(Poly *p, Rat r) {
    p->n = 0;
    if (r.p == 0) return;
    p->n = 1;
    memset(&p->t[0], 0, sizeof p->t[0]);
    p->t[0].c = r;
}

static int same_exp(const Term *a, const Term *b) { return memcmp(a->e, b->e, sizeof a->e) == 0; }

static void add_term(Ctx *c, Poly *p, const Term *t) {
    if (t->c.p == 0) return;
    for (int i = 0; i < p->n; i++)
        if (same_exp(&p->t[i], t)) {
            Rat s;
            if (rat_add(c, p->t[i].c, t->c, &s)) return;
            if (s.p == 0) p->t[i] = p->t[--p->n];
            else p->t[i].c = s;
            return;
        }
    if (p->n >= PT) {
        fail(c, SQM_ERR_FULL);
        return;
    }
    p->t[p->n++] = *t;
}

static void poly_add(Ctx *c, Poly *a, const Poly *b) {
    for (int i = 0; i < b->n && !c->code; i++) add_term(c, a, &b->t[i]);
}

static void poly_neg(Poly *a) {
    for (int i = 0; i < a->n; i++) a->t[i].c.p = -a->t[i].c.p;
}

/* a = a * b, through c->prod */
static void poly_mul(Ctx *c, Poly *a, const Poly *b) {
    Poly *r = &c->prod;
    r->n = 0;
    for (int i = 0; i < a->n && !c->code; i++)
        for (int j = 0; j < b->n && !c->code; j++) {
            Term t;
            if (rat_mul(c, a->t[i].c, b->t[j].c, &t.c)) return;
            for (int k = 0; k < PA; k++) {
                int e = a->t[i].e[k] + b->t[j].e[k];
                if (e > MAXEXP || e < -MAXEXP) {
                    fail(c, SQM_ERR_RANGE);
                    return;
                }
                t.e[k] = (int8_t)e;
            }
            add_term(c, r, &t);
        }
    if (!c->code) *a = *r;
}

static int is_const(const Poly *p, Rat *r) {
    if (p->n == 0) {
        *r = rat(0, 1);
        return 1;
    }
    if (p->n > 1) return 0;
    for (int k = 0; k < PA; k++)
        if (p->t[0].e[k]) return 0;
    *r = p->t[0].c;
    return 1;
}

/* ---- text ---- */

typedef struct {
    char *s;
    size_t n, cap;
    int over;
} Buf;

static void put(Buf *b, const char *s) {
    size_t len = strlen(s);
    if (b->n + len + 1 > b->cap) {
        b->over = 1;
        return;
    }
    memcpy(b->s + b->n, s, len + 1);
    b->n += len;
}

static int is_ident(const char *s) {
    if (!*s) return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
    return 1;
}

/* f(...) as one call, so it needs no parentheses as a factor or a base */
static int is_call(const char *s) {
    const char *p = strchr(s, '(');
    if (!p || p == s) return 0;
    for (const char *q = s; q < p; q++)
        if (!((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') || *q == '_'))
            return 0;
    int depth = 0;
    for (; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')' && --depth == 0) return p[1] == 0;
    }
    return 0;
}

/* names first, then other atoms; alphabetical within each */
static int atom_before(const Ctx *c, int a, int b) {
    int na = is_ident(c->atom[a]), nb = is_ident(c->atom[b]);
    if (na != nb) return na;
    return strcmp(c->atom[a], c->atom[b]) < 0;
}

static void atom_order(const Ctx *c, int *ord) {
    for (int i = 0; i < c->natoms; i++) ord[i] = i;
    for (int i = 1; i < c->natoms; i++)
        for (int j = i; j > 0 && atom_before(c, ord[j], ord[j - 1]); j--) {
            int t = ord[j];
            ord[j] = ord[j - 1];
            ord[j - 1] = t;
        }
}

static int degree(const Term *t) {
    int d = 0;
    for (int k = 0; k < PA; k++)
        if (t->e[k] > 0) d += t->e[k];
    return d;
}

/* higher total degree first, then by the atoms in order, higher powers first */
static int term_before(const int *ord, int na, const Term *a, const Term *b) {
    int da = degree(a), db = degree(b);
    if (da != db) return da > db;
    for (int i = 0; i < na; i++)
        if (a->e[ord[i]] != b->e[ord[i]]) return a->e[ord[i]] > b->e[ord[i]];
    return 0;
}

static void sort_terms(const Ctx *c, Poly *p) {
    int ord[PA];
    atom_order(c, ord);
    for (int i = 1; i < p->n; i++)
        for (int j = i; j > 0 && term_before(ord, c->natoms, &p->t[j], &p->t[j - 1]); j--) {
            Term t = p->t[j];
            p->t[j] = p->t[j - 1];
            p->t[j - 1] = t;
        }
}

static void put_factor(Buf *b, const char *atom, int k) {
    char t[16];
    int simple = is_ident(atom) || is_call(atom);
    if (!simple) put(b, "(");
    put(b, atom);
    if (!simple) put(b, ")");
    if (k != 1) {
        snprintf(t, sizeof t, "^%d", k);
        put(b, t);
    }
}

/* one term without its sign: "3*x^2/(2*y)" */
static void put_term(const Ctx *c, Buf *b, const Term *t) {
    int ord[PA], nnum = 0, nden = 0;
    atom_order(c, ord);
    for (int k = 0; k < c->natoms; k++) {
        nnum += t->e[k] > 0;
        nden += t->e[k] < 0;
    }
    char n[32];
    int64_t p = t->c.p < 0 ? -t->c.p : t->c.p;
    int first = 1;
    if (p != 1 || nnum == 0) {
        snprintf(n, sizeof n, "%lld", (long long)p);
        put(b, n);
        first = 0;
    }
    for (int i = 0; i < c->natoms; i++) {
        int k = ord[i];
        if (t->e[k] <= 0) continue;
        if (!first) put(b, "*");
        put_factor(b, c->atom[k], t->e[k]);
        first = 0;
    }
    int parts = nden + (t->c.q != 1);
    if (parts == 0) return;
    put(b, "/");
    if (parts > 1) put(b, "(");
    first = 1;
    if (t->c.q != 1) {
        snprintf(n, sizeof n, "%lld", (long long)t->c.q);
        put(b, n);
        first = 0;
    }
    for (int i = 0; i < c->natoms; i++) {
        int k = ord[i];
        if (t->e[k] >= 0) continue;
        if (!first) put(b, "*");
        put_factor(b, c->atom[k], -t->e[k]);
        first = 0;
    }
    if (parts > 1) put(b, ")");
}

static void put_poly(const Ctx *c, Buf *b, Poly *p) {
    if (p->n == 0) {
        put(b, "0");
        return;
    }
    sort_terms(c, p);
    for (int i = 0; i < p->n; i++) {
        int negative = p->t[i].c.p < 0;
        if (i == 0) put(b, negative ? "-" : "");
        else put(b, negative ? " - " : " + ");
        put_term(c, b, &p->t[i]);
    }
}

static int poly_text(Ctx *c, Poly *p, char *out, size_t cap) {
    Buf b = {out, 0, cap, 0};
    out[0] = 0;
    put_poly(c, &b, p);
    if (b.over) fail(c, SQM_ERR_FULL);
    return b.over ? -1 : 0;
}

/* ---- atoms ---- */

/* p = 1 * atom^k, with atom the canonical form of text; numbers fold */
static void atom_poly(Ctx *c, const char *text, int k, Poly *p) {
    p->n = 0;
    if (c->code) return;
    int rc = sqm_parse(text, &c->tmp, NULL);
    if (rc == SQM_OK) rc = sqm_simplify(&c->tmp, &c->tmp2);
    if (rc != SQM_OK) {
        fail(c, rc);
        return;
    }
    const SqmNode *root = &c->tmp2.node[c->tmp2.root];
    Rat r;
    if (root->kind == SQM_NUM && rat_from(root->v, &r) == 0 && (k >= 0 || r.p != 0)) {
        if (rat_pow(c, r, k, &r) == 0) poly_const(p, r);
        return;
    }
    char canon[ATOM_LEN];
    if (sqm_print(&c->tmp2, canon, sizeof canon) >= sizeof canon) {
        fail(c, SQM_ERR_FULL);
        return;
    }
    int a = 0;
    while (a < c->natoms && strcmp(c->atom[a], canon)) a++;
    if (a == c->natoms) {
        if (c->natoms >= PA) {
            fail(c, SQM_ERR_FULL);
            return;
        }
        memcpy(c->atom[c->natoms++], canon, sizeof canon);
    }
    if (k > MAXEXP || k < -MAXEXP) {
        fail(c, SQM_ERR_RANGE);
        return;
    }
    p->n = 1;
    memset(&p->t[0], 0, sizeof p->t[0]);
    p->t[0].c = rat(1, 1);
    p->t[0].e[a] = (int8_t)k;
}

/* p^k for a whole k: repeated products, or the inverse of a single term, or
 * a sum kept whole as an atom to a negative power */
static void poly_pow(Ctx *c, Poly *p, int k) {
    if (k >= 0) {
        Poly base = *p;
        poly_const(p, rat(1, 1));
        for (int i = 0; i < k && !c->code; i++) poly_mul(c, p, &base);
        return;
    }
    if (p->n == 1) {
        Term *t = &p->t[0];
        if (rat_pow(c, t->c, k, &t->c)) return;
        for (int a = 0; a < PA; a++) {
            int e = t->e[a] * k;
            if (e > MAXEXP || e < -MAXEXP) {
                fail(c, SQM_ERR_RANGE);
                return;
            }
            t->e[a] = (int8_t)e;
        }
        return;
    }
    char text[ATOM_LEN * 2];
    if (p->n == 0) snprintf(text, sizeof text, "0");
    else if (poly_text(c, p, text, sizeof text)) return;
    atom_poly(c, text, k, p);
}

static void expand(Ctx *c, int i, Poly *out);

/* the expanded text of node i, for a function argument or an exponent */
static int sub_text(Ctx *c, int i, char *out, size_t cap) {
    Poly p;
    expand(c, i, &p);
    if (c->code) return -1;
    return poly_text(c, &p, out, cap);
}

static void expand2(Ctx *c, int i, Poly *out);

/* each level holds a polynomial on the stack, so the depth is bounded */
static void expand(Ctx *c, int i, Poly *out) {
    out->n = 0;
    if (c->code) return;
    if (++c->depth > MAXDEPTH) fail(c, SQM_ERR_DEPTH);
    else expand2(c, i, out);
    c->depth--;
}

static void expand2(Ctx *c, int i, Poly *out) {
    const SqmNode *nd = &c->in->node[i];
    Poly b;
    char text[ATOM_LEN * 2 + 16], x[ATOM_LEN], y[ATOM_LEN];
    switch (nd->kind) {
        case SQM_NUM: {
            Rat r;
            if (rat_from(nd->v, &r) == 0) poly_const(out, r);
            else if (isfinite(nd->v)) {
                snprintf(text, sizeof text, "%.17g", nd->v);
                atom_poly(c, text, 1, out);
            } else fail(c, SQM_ERR_RANGE);
            return;
        }
        case SQM_CONST: atom_poly(c, nd->a == 0 ? "pi" : "e", 1, out); return;
        case SQM_VAR: atom_poly(c, c->in->names[nd->a], 1, out); return;
        case SQM_NEG:
            expand(c, nd->a, out);
            poly_neg(out);
            return;
        case SQM_ADD:
        case SQM_SUB:
            expand(c, nd->a, out);
            expand(c, nd->b, &b);
            if (nd->kind == SQM_SUB) poly_neg(&b);
            poly_add(c, out, &b);
            return;
        case SQM_MUL:
            expand(c, nd->a, out);
            expand(c, nd->b, &b);
            poly_mul(c, out, &b);
            return;
        case SQM_DIV:
            expand(c, nd->a, out);
            expand(c, nd->b, &b);
            poly_pow(c, &b, -1);
            poly_mul(c, out, &b);
            return;
        case SQM_POW: {
            expand(c, nd->b, &b);
            Rat k;
            if (!c->code && is_const(&b, &k) && k.q == 1 && k.p <= MAXPOW && k.p >= -MAXPOW) {
                expand(c, nd->a, out);
                poly_pow(c, out, (int)k.p);
                return;
            }
            if (sub_text(c, nd->a, x, sizeof x) || poly_text(c, &b, y, sizeof y)) return;
            snprintf(text, sizeof text, "(%s)^(%s)", x, y);
            atom_poly(c, text, 1, out);
            return;
        }
        case SQM_FUN:
            if (sub_text(c, nd->a, x, sizeof x)) return;
            if (sqm_fn_args(nd->fn) == 2) {
                if (sub_text(c, nd->b, y, sizeof y)) return;
                snprintf(text, sizeof text, "%s(%s, %s)", sqm_fn_name(nd->fn), x, y);
            } else if (nd->fn == SQM_FN_FACT) {
                snprintf(text, sizeof text, "(%s)!", x);
            } else {
                snprintf(text, sizeof text, "%s(%s)", sqm_fn_name(nd->fn), x);
            }
            atom_poly(c, text, 1, out);
            return;
        default: fail(c, SQM_ERR_SYNTAX); return;
    }
}

static int finish(Ctx *c, const char *text, SqmExpr *out) {
    if (c->code) {
        sqm__clear(out);
        return c->code;
    }
    int rc = sqm_parse(text, out, NULL);
    if (rc != SQM_OK) sqm__clear(out);
    return rc;
}

/* ---- public ---- */

static int begin(Ctx *c, const SqmExpr *in, Poly *p) {
    c->in = in;
    c->natoms = 0;
    c->depth = 0;
    c->code = SQM_OK;
    p->n = 0;
    if (!in || in->root < 0) return c->code = SQM_ERR_SYNTAX;
    expand(c, in->root, p);
    return c->code;
}

int sqm_expand(const SqmExpr *in, SqmExpr *out) {
    Ctx c;
    Poly p;
    char text[TEXT];
    if (in == out) return SQM_ERR_SYNTAX;
    text[0] = 0;
    if (begin(&c, in, &p) == SQM_OK) poly_text(&c, &p, text, sizeof text);
    return finish(&c, text, out);
}

static int find_atom(const Ctx *c, const char *name) {
    for (int a = 0; a < c->natoms; a++)
        if (!strcmp(c->atom[a], name)) return a;
    return -1;
}

/* name appears as a word inside a larger atom: sin(x), x - 1 */
static int inside_atom(const Ctx *c, const char *name, int except) {
    size_t len = strlen(name);
    for (int a = 0; a < c->natoms; a++) {
        if (a == except) continue;
        for (const char *s = c->atom[a]; (s = strstr(s, name)) != NULL; s++) {
            int before = s > c->atom[a] && (isalnum((unsigned char)s[-1]) || s[-1] == '_');
            int after = isalnum((unsigned char)s[len]) || s[len] == '_';
            if (!before && !after) return 1;
        }
    }
    return 0;
}

int sqm_collect(const SqmExpr *in, const char *var, SqmExpr *out) {
    Ctx c;
    Poly p, part;
    char text[TEXT];
    if (in == out || !var) return SQM_ERR_SYNTAX;
    text[0] = 0;
    if (begin(&c, in, &p) != SQM_OK) return finish(&c, text, out);
    int v = find_atom(&c, var);
    if (v < 0) {
        poly_text(&c, &p, text, sizeof text);
        return finish(&c, text, out);
    }
    Buf b = {text, 0, sizeof text, 0};
    int done[PT] = {0}, groups = 0;
    for (;;) {
        /* the highest power of var not written yet */
        int best = -1;
        for (int i = 0; i < p.n; i++)
            if (!done[i] && (best < 0 || p.t[i].e[v] > p.t[best].e[v])) best = i;
        if (best < 0) break;
        int k = p.t[best].e[v];
        part.n = 0;
        for (int i = 0; i < p.n; i++)
            if (!done[i] && p.t[i].e[v] == k) {
                done[i] = 1;
                part.t[part.n] = p.t[i];
                part.t[part.n++].e[v] = 0;
            }
        if (part.n == 1 || k == 0) {
            /* single terms (and the part without var) keep their own signs */
            for (int i = 0; i < part.n; i++) part.t[i].e[v] = (int8_t)k;
            sort_terms(&c, &part);
            for (int i = 0; i < part.n; i++) {
                int negative = part.t[i].c.p < 0;
                put(&b, groups == 0 && i == 0 ? (negative ? "-" : "") : (negative ? " - " : " + "));
                put_term(&c, &b, &part.t[i]);
            }
        } else {
            put(&b, groups ? " + (" : "(");
            put_poly(&c, &b, &part);
            put(&b, k > 0 ? ")*" : ")/");
            put_factor(&b, c.atom[v], k > 0 ? k : -k);
        }
        groups++;
    }
    if (groups == 0) put(&b, "0");
    if (b.over) fail(&c, SQM_ERR_FULL);
    return finish(&c, text, out);
}

/* ---- solving ---- */

#define MAXDEG 32

static void add_solution(SqmSolution *out, int cap, int *n, const char *text, double value) {
    for (int i = 0; i < *n && i < cap; i++)
        if (!strcmp(out[i].text, text)) return;
    if (*n < cap) {
        snprintf(out[*n].text, sizeof out[*n].text, "%s", text);
        out[*n].value = value;
    }
    (*n)++;
}

static void add_rat(SqmSolution *out, int cap, int *n, Rat r) {
    char t[48];
    rat_text(r, t, sizeof t);
    add_solution(out, cap, n, t, rat_val(r));
}

/* value of the polynomial a[0..d] at r, exactly; -1 on overflow */
static int horner(Ctx *c, const Rat *a, int d, Rat r, Rat *v) {
    Rat s = a[d];
    for (int k = d - 1; k >= 0; k--)
        if (rat_mul(c, s, r, &s) || rat_add(c, s, a[k], &s)) return -1;
    *v = s;
    return 0;
}

/* divide a[0..d] by (x - r), which is a root: the quotient goes to a[0..d-1] */
static int deflate(Ctx *c, Rat *a, int d, Rat r) {
    Rat q = a[d];
    for (int k = d - 1; k >= 0; k--) {
        Rat orig = a[k];
        a[k] = q;
        if (k > 0 && (rat_mul(c, q, r, &q) || rat_add(c, q, orig, &q))) return -1;
    }
    return 0;
}

/* largest s with s^2 dividing m (m > 0), and m / s^2 */
static void square_part(int64_t m, int64_t *s, int64_t *rest) {
    int64_t sq = 1;
    for (int64_t k = 2; k <= 1000000 && k * k <= m; k++)
        while (m % (k * k) == 0) {
            m /= k * k;
            sq *= k;
        }
    int64_t r = (int64_t)llround(sqrt((double)m));
    for (int64_t t = r - 1; t <= r + 1; t++)
        if (t > 1 && t * t == m) {
            sq *= t;
            m = 1;
        }
    *s = sq;
    *rest = m;
}

/* a x^2 + b x + c = 0 with exact fractions */
static int quadratic(Ctx *c, Rat a, Rat b, Rat cc, SqmSolution *out, int cap, int *n) {
    Rat b2, ac, d, u, two_a;
    if (rat_mul(c, b, b, &b2) || rat_mul(c, a, cc, &ac) || rat_mul(c, ac, rat(-4, 1), &ac) || rat_add(c, b2, ac, &d) ||
        rat_mul(c, a, rat(2, 1), &two_a) || rat_mul(c, b, rat(-1, 1), &u) || rat_mul(c, u, rat(two_a.q, two_a.p), &u))
        return -1;
    if (d.p < 0) return 0; /* no real roots */
    if (d.p == 0) {
        add_rat(out, cap, n, u);
        return 0;
    }
    /* sqrt(p/q) = sqrt(p q)/q = s sqrt(r)/q */
    int64_t m, s, r;
    if (mul64(d.p, d.q, &m)) {
        fail(c, SQM_ERR_RANGE);
        return -1;
    }
    square_part(m, &s, &r);
    Rat v; /* the root is u -+ v sqrt(r), v > 0 */
    if (rat_mul(c, rat(s, d.q), rat(two_a.q, two_a.p), &v)) return -1;
    if (v.p < 0) v.p = -v.p;
    if (r == 1) {
        Rat lo, hi;
        if (rat_add(c, u, rat(-v.p, v.q), &lo) || rat_add(c, u, v, &hi)) return -1;
        add_rat(out, cap, n, lo);
        add_rat(out, cap, n, hi);
        return 0;
    }
    int64_t l = u.q / gcd64(u.q, v.q), U, V;
    if (mul64(l, v.q, &l) || mul64(u.p, l / u.q, &U) || mul64(v.p, l / v.q, &V)) {
        fail(c, SQM_ERR_RANGE);
        return -1;
    }
    char core[48], t[2][128];
    if (V == 1) snprintf(core, sizeof core, "sqrt(%lld)", (long long)r);
    else snprintf(core, sizeof core, "%lld*sqrt(%lld)", (long long)V, (long long)r);
    for (int sgn = 0; sgn < 2; sgn++) {
        const char *op = sgn ? " + " : " - ";
        if (U == 0 && l == 1) snprintf(t[sgn], sizeof t[sgn], "%s%s", sgn ? "" : "-", core);
        else if (U == 0) snprintf(t[sgn], sizeof t[sgn], "%s%s/%lld", sgn ? "" : "-", core, (long long)l);
        else if (l == 1) snprintf(t[sgn], sizeof t[sgn], "%lld%s%s", (long long)U, op, core);
        else snprintf(t[sgn], sizeof t[sgn], "(%lld%s%s)/%lld", (long long)U, op, core, (long long)l);
    }
    double root = sqrt((double)r), uv = rat_val(u), vv = rat_val(v);
    add_solution(out, cap, n, t[0], uv - vv * root);
    add_solution(out, cap, n, t[1], uv + vv * root);
    return 0;
}

/* every rational root of a whole-number polynomial a[0..d] (a[0] != 0),
 * deflating as it goes; returns the degree left */
static int rational_roots(Ctx *c, Rat *a, int d, SqmSolution *out, int cap, int *n) {
    int64_t a0 = a[0].p < 0 ? -a[0].p : a[0].p, ad = a[d].p < 0 ? -a[d].p : a[d].p;
    if (a0 > (int64_t)1e12 || ad > (int64_t)1e12) return d;
    for (int64_t p = 1; p * p <= a0 && d > 2; p++) {
        if (a0 % p) continue;
        int64_t ps[2] = {p, a0 / p};
        for (int i = 0; i < 2; i++)
            for (int64_t q = 1; q * q <= ad && d > 2; q++) {
                if (ad % q) continue;
                int64_t qs[2] = {q, ad / q};
                for (int j = 0; j < 2; j++)
                    for (int sgn = -1; sgn <= 1 && d > 2; sgn += 2) {
                        Rat r = rat(sgn * ps[i], qs[j]), v;
                        for (;;) {
                            int ok = d > 2 && horner(c, a, d, r, &v) == 0 && v.p == 0;
                            c->code = SQM_OK; /* a candidate that overflows is just not a root */
                            if (!ok) break;
                            add_rat(out, cap, n, r);
                            if (deflate(c, a, d, r)) return -1;
                            d--;
                        }
                    }
            }
    }
    return d;
}

static int by_value(const void *x, const void *y) {
    double a = ((const SqmSolution *)x)->value, b = ((const SqmSolution *)y)->value;
    if (a != a || b != b) return (a != a) - (b != b);
    return (a > b) - (a < b);
}

/* a formula in the other atoms, simplified and printed */
static void add_formula(Ctx *c, const char *text, SqmSolution *out, int cap, int *n) {
    char t[sizeof out[0].text];
    if (sqm_parse(text, &c->tmp, NULL) != SQM_OK || sqm_simplify(&c->tmp, &c->tmp2) != SQM_OK) {
        fail(c, SQM_ERR_FULL);
        return;
    }
    if (sqm_print(&c->tmp2, t, sizeof t) >= sizeof t) {
        fail(c, SQM_ERR_FULL);
        return;
    }
    double v;
    if (sqm_eval(&c->tmp2, NULL, NULL, 0, &v) != SQM_OK) v = NAN;
    add_solution(out, cap, n, t, v);
}

int sqm_solve_for(const SqmExpr *eq, const char *var, SqmSolution *out, int cap, int *count) {
    Ctx c;
    Poly p;
    int n = 0;
    *count = 0;
    if (!var || !out || cap < 0) return SQM_ERR_SYNTAX;
    if (begin(&c, eq, &p) != SQM_OK) return c.code;
    int v = find_atom(&c, var);
    if (v < 0 || p.n == 0) {
        *count = p.n == 0 ? -1 : 0; /* 0 = 0 holds for every value; 1 = 0 for none */
        return SQM_OK;
    }
    if (inside_atom(&c, var, v)) return SQM_ERR_SOLVE;
    /* powers of var, shifted so the lowest is 0 */
    int lo = MAXEXP, hi = -MAXEXP;
    for (int i = 0; i < p.n; i++) {
        if (p.t[i].e[v] < lo) lo = p.t[i].e[v];
        if (p.t[i].e[v] > hi) hi = p.t[i].e[v];
    }
    if (lo > 0) add_solution(out, cap, &n, "0", 0); /* x^lo divides it */
    int d = hi - lo;
    if (d > MAXDEG) {
        *count = n < cap ? n : cap;
        return SQM_ERR_SOLVE;
    }
    /* coefficients: numbers when nothing else is left */
    Rat a[MAXDEG + 1];
    int numeric = 1;
    for (int k = 0; k <= d; k++) a[k] = rat(0, 1);
    for (int i = 0; i < p.n; i++) {
        for (int at = 0; at < c.natoms; at++)
            if (at != v && p.t[i].e[at]) numeric = 0;
        int k = p.t[i].e[v] - lo;
        if (numeric && rat_add(&c, a[k], p.t[i].c, &a[k])) return c.code;
    }
    int rc = SQM_OK;
    if (numeric) {
        /* whole-number coefficients, then rational roots, then the formula */
        int64_t l = 1;
        for (int k = 0; k <= d; k++) {
            int64_t g = gcd64(l, a[k].q);
            if (mul64(l / g, a[k].q, &l)) return SQM_ERR_RANGE;
        }
        for (int k = 0; k <= d; k++)
            if (rat_mul(&c, a[k], rat(l, 1), &a[k])) return c.code;
        d = rational_roots(&c, a, d, out, cap, &n);
        if (d < 0) return c.code;
        if (d == 1) {
            Rat r;
            if (rat_mul(&c, a[0], rat(-a[1].q, a[1].p), &r)) return c.code;
            add_rat(out, cap, &n, r);
        } else if (d == 2) {
            if (quadratic(&c, a[2], a[1], a[0], out, cap, &n)) return c.code ? c.code : SQM_ERR_RANGE;
        } else if (d > 2) {
            rc = SQM_ERR_SOLVE;
        }
    } else if (d == 1 || d == 2) {
        /* the other atoms stay symbols: -c0/c1, or the quadratic formula */
        Poly k[3];
        char t[3][TEXT / 4], f[TEXT / 4 * 5 + 64];
        for (int j = 0; j <= d; j++) k[j].n = 0;
        for (int i = 0; i < p.n; i++) {
            Term tm = p.t[i];
            int j = tm.e[v] - lo;
            tm.e[v] = 0;
            add_term(&c, &k[j], &tm);
        }
        for (int j = 0; j <= d; j++)
            if (poly_text(&c, &k[j], t[j], sizeof t[j])) return c.code;
        if (d == 1) {
            snprintf(f, sizeof f, "-(%s)/(%s)", t[0], t[1]);
            add_formula(&c, f, out, cap, &n);
        } else {
            for (int sgn = 0; sgn < 2; sgn++) {
                snprintf(f, sizeof f, "(-(%s) %c sqrt((%s)^2 - 4*(%s)*(%s)))/(2*(%s))", t[1], sgn ? '+' : '-', t[1],
                         t[2], t[0], t[2]);
                add_formula(&c, f, out, cap, &n);
            }
        }
        if (c.code) return c.code;
    } else if (d > 2) {
        rc = SQM_ERR_SOLVE;
    }
    *count = n < cap ? n : cap;
    qsort(out, (size_t)*count, sizeof *out, by_value);
    return rc;
}

/* ---- equations ---- */

/* copy node i of src into dst; its index or -1 */
static int graft(SqmExpr *dst, const SqmExpr *src, int i) {
    const SqmNode *nd = &src->node[i];
    int a = nd->a, b = nd->b;
    switch (nd->kind) {
        case SQM_NUM:
        case SQM_CONST: break;
        case SQM_VAR: {
            const char *name = src->names[nd->a];
            if ((a = sqm__name(dst, name, strlen(name))) < 0) return -1;
            break;
        }
        case SQM_NEG:
            if ((a = graft(dst, src, a)) < 0) return -1;
            break;
        case SQM_FUN:
            if ((a = graft(dst, src, a)) < 0) return -1;
            if (sqm_fn_args(nd->fn) == 2 && (b = graft(dst, src, b)) < 0) return -1;
            break;
        default:
            if ((a = graft(dst, src, a)) < 0 || (b = graft(dst, src, b)) < 0) return -1;
    }
    return sqm__node(dst, nd->kind, nd->fn, a, b, nd->v);
}

int sqm_parse_eq(const char *text, SqmExpr *out, SqmError *err) {
    SqmError local;
    if (!err) err = &local;
    const char *eq = strchr(text, '=');
    if (!eq) return sqm_parse(text, out, err);
    if (strchr(eq + 1, '=')) {
        err->code = SQM_ERR_SYNTAX;
        err->pos = (int)(strchr(eq + 1, '=') - text);
        snprintf(err->msg, sizeof err->msg, "more than one =");
        sqm__clear(out);
        return err->code;
    }
    char left[512];
    size_t len = (size_t)(eq - text);
    if (len >= sizeof left) {
        err->code = SQM_ERR_FULL;
        err->pos = 0;
        snprintf(err->msg, sizeof err->msg, "expression too large");
        sqm__clear(out);
        return err->code;
    }
    memcpy(left, text, len);
    left[len] = 0;
    SqmExpr right;
    int rc = sqm_parse(left, out, err);
    if (rc != SQM_OK) return rc;
    rc = sqm_parse(eq + 1, &right, err);
    if (rc != SQM_OK) {
        err->pos += (int)len + 1;
        sqm__clear(out);
        return rc;
    }
    int r = graft(out, &right, right.root);
    int root = r < 0 ? -1 : sqm__node(out, SQM_SUB, 0, out->root, r, 0);
    if (root < 0) {
        err->code = SQM_ERR_FULL;
        err->pos = 0;
        snprintf(err->msg, sizeof err->msg, "expression too large");
        sqm__clear(out);
        return err->code;
    }
    out->root = root;
    return SQM_OK;
}
