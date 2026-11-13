/* calcgeom: points, lines, circles and polygons on the graph (see calcgeom.h). Original code. */
#include "calcgeom.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqmath.h"

#define PI 3.14159265358979323846

enum { HOW_FREE, HOW_THROUGH, HOW_CIRCLE_R, HOW_CIRCLE_P, HOW_CIRCLE3, HOW_MID, HOW_INTER, HOW_PERP, HOW_PARALLEL,
       HOW_BISECT };

static SqmExpr scratch;

static int fail(char *msg, size_t cap, const char *fmt, const char *a) {
    if (msg && cap) snprintf(msg, cap, fmt, a ? a : "");
    return -1;
}

static int is_ident(int ch) { return isalnum((unsigned char)ch) || ch == '_'; }

static int linelike(int kind) { return kind == CALC_G_SEGMENT || kind == CALC_G_LINE || kind == CALC_G_RAY; }

static const char *kind_name(int kind) {
    static const char *const N[] = {"point", "segment", "line", "ray", "circle", "polygon"};
    return kind >= 0 && kind <= CALC_G_POLYGON ? N[kind] : "?";
}

void calc_geom_init(CalcGeom *g) { memset(g, 0, sizeof *g); }

int calc_geom_find(const CalcGeom *g, const char *name) {
    for (int i = 0; i < g->n; i++)
        if (!strcmp(g->o[i].name, name)) return i;
    return -1;
}

/* ---- working objects out ---- */

static int value_of(const char *text, const CalcEnv *env, const CalcSheet *s, double *v, char *why, size_t cap) {
    char buf[600];
    if (calc_expand(text, buf, sizeof buf, s, env, why, cap)) return -1;
    SqmError e;
    if (sqm_parse(buf, &scratch, &e) != SQM_OK) return fail(why, cap, "%.40s", e.msg);
    if (sqm_eval(&scratch, env ? env->names : NULL, env ? env->values : NULL, env ? env->n : 0, v) != SQM_OK) {
        for (int k = 0; k < scratch.nnames; k++) {
            int known = 0;
            for (int j = 0; env && j < env->n; j++) known |= !strcmp(env->names[j], scratch.names[k]);
            if (!known) return fail(why, cap, "define %s first", scratch.names[k]);
        }
        return fail(why, cap, "no value", NULL);
    }
    if (!isfinite(*v)) return fail(why, cap, "%.30s is undefined", text);
    return 0;
}

/* where line-like a meets b (line-like or circle): up to two points in
 * order along a (for two circles: the left of the line between centres
 * first). Returns how many. */
static int in_range(int kind, double t) {
    const double eps = 1e-9;
    if (kind == CALC_G_SEGMENT) return t >= -eps && t <= 1 + eps;
    if (kind == CALC_G_RAY) return t >= -eps;
    return 1;
}

static int meet(const CalcGeomObj *a, const CalcGeomObj *b, double xs[2], double ys[2]) {
    if (a->kind == CALC_G_CIRCLE && linelike(b->kind)) return meet(b, a, xs, ys);
    if (linelike(a->kind) && linelike(b->kind)) {
        double dx = a->x2 - a->x, dy = a->y2 - a->y, ex = b->x2 - b->x, ey = b->y2 - b->y;
        double cr = dx * ey - dy * ex;
        if (fabs(cr) <= 1e-12 * hypot(dx, dy) * hypot(ex, ey)) return 0;
        double cx = b->x - a->x, cy = b->y - a->y;
        double t = (cx * ey - cy * ex) / cr, u = (cx * dy - cy * dx) / cr;
        if (!in_range(a->kind, t) || !in_range(b->kind, u)) return 0;
        xs[0] = a->x + t * dx;
        ys[0] = a->y + t * dy;
        return 1;
    }
    if (linelike(a->kind) && b->kind == CALC_G_CIRCLE) {
        double dx = a->x2 - a->x, dy = a->y2 - a->y, fx = a->x - b->x, fy = a->y - b->y;
        double qa = dx * dx + dy * dy, qb = 2 * (dx * fx + dy * fy), qc = fx * fx + fy * fy - b->r * b->r;
        double disc = qb * qb - 4 * qa * qc;
        if (qa <= 0 || disc < -1e-12 * qb * qb) return 0;
        if (disc < 0) disc = 0;
        double sq = sqrt(disc), t[2] = {(-qb - sq) / (2 * qa), (-qb + sq) / (2 * qa)};
        int n = 0;
        for (int k = 0; k < (sq > 0 ? 2 : 1); k++)
            if (in_range(a->kind, t[k])) {
                xs[n] = a->x + t[k] * dx;
                ys[n] = a->y + t[k] * dy;
                n++;
            }
        return n;
    }
    if (a->kind == CALC_G_CIRCLE && b->kind == CALC_G_CIRCLE) {
        double dx = b->x - a->x, dy = b->y - a->y, d = hypot(dx, dy);
        if (d <= 0 || d > a->r + b->r + 1e-12 * d || d < fabs(a->r - b->r) - 1e-12 * d) return 0;
        double l = (a->r * a->r - b->r * b->r + d * d) / (2 * d), h2 = a->r * a->r - l * l;
        double h = h2 > 0 ? sqrt(h2) : 0;
        double mx = a->x + l * dx / d, my = a->y + l * dy / d;
        xs[0] = mx - h * dy / d;
        ys[0] = my + h * dx / d;
        if (h <= 1e-12 * d) return 1;
        xs[1] = mx + h * dy / d;
        ys[1] = my - h * dx / d;
        return 2;
    }
    return 0;
}

/* whether o's parts are the kinds its way of making needs; else why */
static int kinds_ok(const CalcGeom *g, const CalcGeomObj *o, char *why, size_t cap) {
    int want_points = o->how == HOW_THROUGH || o->how == HOW_CIRCLE_P || o->how == HOW_CIRCLE3 ||
                      o->how == HOW_BISECT || (o->how == HOW_MID && o->npar == 2);
    for (int k = 0; k < o->npar; k++) {
        const CalcGeomObj *p = &g->o[o->par[k]];
        int kind = p->kind, good = 1;
        if (want_points || (o->how == HOW_CIRCLE_R && k == 0)) good = kind == CALC_G_POINT;
        if (o->how == HOW_MID && o->npar == 1) good = kind == CALC_G_SEGMENT;
        if (o->how == HOW_INTER) good = linelike(kind) || kind == CALC_G_CIRCLE;
        if (o->how == HOW_PERP || o->how == HOW_PARALLEL) good = k == 0 ? linelike(kind) : kind == CALC_G_POINT;
        if (!good) {
            char nm[CALC_GEOM_NAME];
            memcpy(nm, p->name, sizeof nm);
            snprintf(why, cap, "%.15s is a %s", nm, kind_name(kind));
            return 0;
        }
    }
    return 1;
}

static void compute(CalcGeom *g, int i, const CalcEnv *env, const CalcSheet *s) {
    CalcGeomObj *o = &g->o[i];
    o->ok = 0;
    o->why[0] = 0;
    const CalcGeomObj *p[CALC_GEOM_POLY] = {0};
    for (int k = 0; k < o->npar; k++) {
        p[k] = &g->o[o->par[k]];
        if (!p[k]->ok) {
            char nm[CALC_GEOM_NAME];
            memcpy(nm, p[k]->name, sizeof nm);
            snprintf(o->why, sizeof o->why, "%.15s does not exist now", nm);
            return;
        }
    }
    if (!kinds_ok(g, o, o->why, sizeof o->why)) return;
    char why[48] = "";
    switch (o->how) {
    case HOW_FREE:
        if (value_of(o->ex[0], env, s, &o->x, why, sizeof why) || value_of(o->ex[1], env, s, &o->y, why, sizeof why)) {
            snprintf(o->why, sizeof o->why, "%s", why);
            return;
        }
        break;
    case HOW_THROUGH:
        for (int k = 0; k < o->npar; k++)
            for (int j = 0; j < k; j++)
                if (p[k]->x == p[j]->x && p[k]->y == p[j]->y) {
                    char a[CALC_GEOM_NAME], b[CALC_GEOM_NAME];
                    memcpy(a, p[j]->name, sizeof a);
                    memcpy(b, p[k]->name, sizeof b);
                    snprintf(o->why, sizeof o->why, "%.10s and %.10s are the same point", a, b);
                    return;
                }
        o->x = p[0]->x;
        o->y = p[0]->y;
        o->x2 = p[1]->x;
        o->y2 = p[1]->y;
        break;
    case HOW_CIRCLE_R:
        o->x = p[0]->x;
        o->y = p[0]->y;
        if (value_of(o->ex[0], env, s, &o->r, why, sizeof why)) {
            snprintf(o->why, sizeof o->why, "%s", why);
            return;
        }
        if (!(o->r > 0)) {
            snprintf(o->why, sizeof o->why, "the radius is not above 0");
            return;
        }
        break;
    case HOW_CIRCLE_P:
        o->x = p[0]->x;
        o->y = p[0]->y;
        o->r = hypot(p[1]->x - p[0]->x, p[1]->y - p[0]->y);
        if (!(o->r > 0)) {
            snprintf(o->why, sizeof o->why, "the points are the same");
            return;
        }
        break;
    case HOW_CIRCLE3: {
        double ax = p[0]->x, ay = p[0]->y, bx = p[1]->x - ax, by = p[1]->y - ay, cx = p[2]->x - ax, cy = p[2]->y - ay;
        double d = 2 * (bx * cy - by * cx);
        double scale = (bx * bx + by * by) + (cx * cx + cy * cy);
        if (fabs(d) <= 1e-12 * scale) {
            snprintf(o->why, sizeof o->why, "the points are on one line");
            return;
        }
        double ux = (cy * (bx * bx + by * by) - by * (cx * cx + cy * cy)) / d;
        double uy = (bx * (cx * cx + cy * cy) - cx * (bx * bx + by * by)) / d;
        o->x = ax + ux;
        o->y = ay + uy;
        o->r = hypot(ux, uy);
        break;
    }
    case HOW_MID:
        if (o->npar == 1) {
            o->x = (p[0]->x + p[0]->x2) / 2;
            o->y = (p[0]->y + p[0]->y2) / 2;
        } else {
            o->x = (p[0]->x + p[1]->x) / 2;
            o->y = (p[0]->y + p[1]->y) / 2;
        }
        break;
    case HOW_INTER: {
        double xs[2], ys[2];
        int n = meet(p[0], p[1], xs, ys);
        if (n < o->which) {
            snprintf(o->why, sizeof o->why, n ? "they meet only once" : "they do not meet");
            return;
        }
        o->x = xs[o->which - 1];
        o->y = ys[o->which - 1];
        break;
    }
    case HOW_PERP:
    case HOW_PARALLEL: {
        double dx = p[0]->x2 - p[0]->x, dy = p[0]->y2 - p[0]->y;
        o->x = p[1]->x;
        o->y = p[1]->y;
        if (o->how == HOW_PERP) {
            o->x2 = o->x - dy;
            o->y2 = o->y + dx;
        } else {
            o->x2 = o->x + dx;
            o->y2 = o->y + dy;
        }
        break;
    }
    case HOW_BISECT: {
        double mx = (p[0]->x + p[1]->x) / 2, my = (p[0]->y + p[1]->y) / 2;
        double dx = p[1]->x - p[0]->x, dy = p[1]->y - p[0]->y;
        if (dx == 0 && dy == 0) {
            snprintf(o->why, sizeof o->why, "the points are the same");
            return;
        }
        o->x = mx;
        o->y = my;
        o->x2 = mx - dy;
        o->y2 = my + dx;
        break;
    }
    }
    o->ok = 1;
}

void calc_geom_update(CalcGeom *g, const CalcEnv *env, const CalcSheet *s) {
    for (int i = 0; i < g->n; i++) compute(g, i, env, s);
}

/* ---- definitions ---- */

typedef struct {
    const char *p;
    size_t n;
} Span;

static Span trim(const char *p, size_t n) {
    while (n && *p == ' ') {
        p++;
        n--;
    }
    while (n && p[n - 1] == ' ') n--;
    Span s = {p, n};
    return s;
}

/* the top-level comma-separated parts of p[0..n) */
static int split(const char *p, size_t n, Span *out, int cap) {
    int k = 0, depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && (p[i] == '(' || p[i] == '[' || p[i] == '{')) depth++;
        if (i < n && (p[i] == ')' || p[i] == ']' || p[i] == '}')) depth--;
        if (i == n || (p[i] == ',' && depth == 0)) {
            if (k >= cap) return -1;
            out[k++] = trim(p + start, i - start);
            start = i + 1;
        }
    }
    return k;
}

/* "name(args)" filling the whole text: the name and the inside */
static int call_form(const char *t, char *fn, size_t fncap, Span *inside) {
    size_t n = 0;
    while (is_ident(t[n])) n++;
    if (!n || n >= fncap || isdigit((unsigned char)t[0])) return 0;
    const char *q = t + n;
    q += strspn(q, " ");
    if (*q != '(') return 0;
    int depth = 0;
    const char *e = q;
    for (; *e; e++) {
        if (*e == '(') depth++;
        else if (*e == ')' && --depth == 0) break;
    }
    if (!*e || e[1 + strspn(e + 1, " ")]) return 0;
    memcpy(fn, t, n);
    fn[n] = 0;
    inside->p = q + 1;
    inside->n = (size_t)(e - q - 1);
    return 1;
}

static const char *const DEFS[] = {"point",  "segment",      "line",     "ray",       "polygon", "circle",
                                   "midpoint", "intersect", "perpendicular", "parallel", "bisector"};

static int span_is(Span s, const char *word) { return strlen(word) == s.n && !strncmp(s.p, word, s.n); }

/* an argument naming an object defined before index limit; -1 if not */
static int obj_arg(const CalcGeom *g, Span a, int limit) {
    char name[CALC_GEOM_NAME];
    if (!a.n || a.n >= sizeof name) return -1;
    memcpy(name, a.p, a.n);
    name[a.n] = 0;
    int i = calc_geom_find(g, name);
    return i >= 0 && i < limit ? i : -1;
}

int calc_geom_define(CalcGeom *g, const char *name, const char *rhs, const CalcEnv *env, const CalcSheet *s,
                     char *msg, size_t msgcap) {
    if (msg && msgcap) msg[0] = 0;
    const char *t = rhs + strspn(rhs, " ");
    char text[256];
    snprintf(text, sizeof text, "%s", t);
    size_t tl = strlen(text);
    while (tl && text[tl - 1] == ' ') text[--tl] = 0;
    CalcGeomObj o;
    memset(&o, 0, sizeof o);
    o.which = 1;
    Span args[CALC_GEOM_POLY + 1];
    int nargs;
    char fn[24];
    Span inside;
    int at = calc_geom_find(g, name), used = 0;
    for (int k = at + 1; at >= 0 && k < g->n; k++)
        for (int j = 0; j < g->o[k].npar; j++) used |= g->o[k].par[j] == at;
    /* nothing is built on it: it may be remade from anything, and moves to the end */
    int limit = at >= 0 && used ? at : g->n;
    if (text[0] == '(') { /* (x, y) */
        int depth = 0;
        size_t e = 0;
        for (; text[e]; e++) {
            if (text[e] == '(') depth++;
            else if (text[e] == ')' && --depth == 0) break;
        }
        if (!text[e] || text[e + 1]) return -2;
        nargs = split(text + 1, e - 1, args, 3);
        if (nargs != 2) return -2;
        snprintf(fn, sizeof fn, "point");
    } else {
        if (!call_form(text, fn, sizeof fn, &inside)) return -2;
        int known = 0;
        for (size_t k = 0; k < sizeof DEFS / sizeof DEFS[0]; k++) known |= !strcmp(fn, DEFS[k]);
        if (!known) return -2;
        nargs = split(inside.p, inside.n, args, CALC_GEOM_POLY + 1);
        if (nargs < 0) return fail(msg, msgcap, "%s: too many parts", fn);
    }
    for (int k = 0; k < nargs; k++)
        if (!args[k].n) return fail(msg, msgcap, "%s: a part is missing", fn);
    if (strlen(name) >= CALC_GEOM_NAME) return fail(msg, msgcap, "%s: the name is too long", name);
    /* parents: every argument that names an earlier object */
    int obj[CALC_GEOM_POLY + 1];
    for (int k = 0; k < nargs; k++) {
        obj[k] = obj_arg(g, args[k], limit);
        if (obj[k] >= 0 && obj[k] == at) return fail(msg, msgcap, "%s cannot be made from itself", name);
        if (obj[k] < 0 && at >= 0) {
            char nm[CALC_GEOM_NAME];
            if (args[k].n < sizeof nm) {
                memcpy(nm, args[k].p, args[k].n);
                nm[args[k].n] = 0;
                if (!strcmp(nm, name)) return fail(msg, msgcap, "%s cannot be made from itself", name);
                if (calc_geom_find(g, nm) >= limit)
                    return fail(msg, msgcap, "%s is defined after this, so it cannot be used here", nm);
            }
        }
    }
#define NEED_OBJECTS(lo, hi)                                                                                           \
    do {                                                                                                               \
        if (nargs < (lo) || nargs > (hi)) return fail(msg, msgcap, "%s: wrong number of parts", fn);                   \
        for (int k_ = 0; k_ < nargs; k_++)                                                                             \
            if (obj[k_] < 0) {                                                                                         \
                char nm_[40];                                                                                          \
                snprintf(nm_, sizeof nm_, "%.*s", (int)(args[k_].n < 30 ? args[k_].n : 30), args[k_].p);               \
                return fail(msg, msgcap, "%s is not a point, line or circle", nm_);                                    \
            }                                                                                                          \
    } while (0)
    if (!strcmp(fn, "point")) {
        if (nargs != 2) return fail(msg, msgcap, "a point is (x, y)", NULL);
        o.kind = CALC_G_POINT;
        o.how = HOW_FREE;
        for (int k = 0; k < 2; k++) {
            if (args[k].n >= CALC_GEOM_EXPR) return fail(msg, msgcap, "a coordinate is too long", NULL);
            memcpy(o.ex[k], args[k].p, args[k].n);
        }
    } else if (!strcmp(fn, "segment") || !strcmp(fn, "line") || !strcmp(fn, "ray")) {
        NEED_OBJECTS(2, 2);
        o.kind = fn[0] == 's' ? CALC_G_SEGMENT : fn[0] == 'l' ? CALC_G_LINE : CALC_G_RAY;
        o.how = HOW_THROUGH;
    } else if (!strcmp(fn, "polygon")) {
        NEED_OBJECTS(3, CALC_GEOM_POLY);
        o.kind = CALC_G_POLYGON;
        o.how = HOW_THROUGH;
    } else if (!strcmp(fn, "circle")) {
        if (nargs < 2 || nargs > 3) return fail(msg, msgcap, "circle(A, r), circle(A, B) or circle(A, B, C)", NULL);
        o.kind = CALC_G_CIRCLE;
        if (obj[0] < 0) return fail(msg, msgcap, "a circle's centre is a point", NULL);
        if (nargs == 3) {
            NEED_OBJECTS(3, 3);
            o.how = HOW_CIRCLE3;
        } else if (obj[1] >= 0) {
            o.how = HOW_CIRCLE_P;
        } else {
            if (args[1].n >= CALC_GEOM_EXPR) return fail(msg, msgcap, "the radius is too long", NULL);
            o.how = HOW_CIRCLE_R;
            memcpy(o.ex[0], args[1].p, args[1].n);
            nargs = 1;
        }
    } else if (!strcmp(fn, "midpoint")) {
        NEED_OBJECTS(1, 2);
        o.kind = CALC_G_POINT;
        o.how = HOW_MID;
    } else if (!strcmp(fn, "intersect")) {
        if (nargs == 3) {
            if (span_is(args[2], "1") || span_is(args[2], "2")) o.which = args[2].p[0] - '0';
            else return fail(msg, msgcap, "intersect(f, g, 1) or intersect(f, g, 2)", NULL);
            nargs = 2;
        }
        NEED_OBJECTS(2, 2);
        o.kind = CALC_G_POINT;
        o.how = HOW_INTER;
    } else if (!strcmp(fn, "perpendicular") || !strcmp(fn, "parallel")) {
        NEED_OBJECTS(2, 2);
        o.kind = CALC_G_LINE;
        o.how = fn[1] == 'e' ? HOW_PERP : HOW_PARALLEL;
    } else { /* bisector */
        NEED_OBJECTS(2, 2);
        o.kind = CALC_G_LINE;
        o.how = HOW_BISECT;
    }
#undef NEED_OBJECTS
    for (int k = 0; k < nargs && k < CALC_GEOM_POLY; k++)
        if (obj[k] >= 0) o.par[o.npar++] = obj[k];
    snprintf(o.name, sizeof o.name, "%s", name);
    if (!kinds_ok(g, &o, msg ? msg : o.why, msg ? msgcap : sizeof o.why)) return -1;
    if (at >= 0 && !used) { /* out of its old place, to the end */
        for (int k = 0; k < o.npar; k++)
            if (o.par[k] > at) o.par[k]--;
        for (int k = at; k + 1 < g->n; k++) {
            g->o[k] = g->o[k + 1];
            for (int j = 0; j < g->o[k].npar; j++)
                if (g->o[k].par[j] > at) g->o[k].par[j]--;
        }
        g->n--;
        at = -1;
    }
    if (at < 0) {
        if (g->n >= CALC_GEOM_MAX) return fail(msg, msgcap, "no room for more objects", NULL);
        at = g->n++;
    }
    g->o[at] = o;
    calc_geom_update(g, env, s);
    if (!g->o[at].ok) return fail(msg, msgcap, "%s", g->o[at].why);
    return 0;
}

int calc_geom_delete(CalcGeom *g, const char *name) {
    int i = calc_geom_find(g, name);
    if (i < 0) return 0;
    int gone[CALC_GEOM_MAX] = {0}, n = 0;
    gone[i] = 1;
    for (int k = i + 1; k < g->n; k++)
        for (int j = 0; j < g->o[k].npar; j++)
            if (gone[g->o[k].par[j]]) gone[k] = 1;
    int map[CALC_GEOM_MAX], m = 0;
    for (int k = 0; k < g->n; k++) {
        if (gone[k]) {
            n++;
            continue;
        }
        map[k] = m;
        g->o[m] = g->o[k];
        for (int j = 0; j < g->o[m].npar; j++) g->o[m].par[j] = map[g->o[m].par[j]];
        m++;
    }
    g->n = m;
    return n;
}

int calc_geom_draggable(const CalcGeom *g, int i) {
    if (i < 0 || i >= g->n || g->o[i].how != HOW_FREE) return 0;
    for (int k = 0; k < 2; k++) {
        const char *e = g->o[i].ex[k];
        char *end;
        strtod(e, &end);
        if (end == e || end[strspn(end, " ")] || isalpha((unsigned char)e[strspn(e, " +-")])) return 0;
    }
    return 1;
}

int calc_geom_move(CalcGeom *g, int i, double x, double y, const CalcEnv *env, const CalcSheet *s) {
    if (!calc_geom_draggable(g, i) || !isfinite(x) || !isfinite(y)) return -1;
    CalcGeomObj *o = &g->o[i];
    sqm_format(x, 8, o->ex[0], sizeof o->ex[0]);
    sqm_format(y, 8, o->ex[1], sizeof o->ex[1]);
    calc_geom_update(g, env, s);
    return 0;
}

/* ---- measurements ---- */

static double poly_area(const CalcGeom *g, const CalcGeomObj *o) {
    double a = 0;
    for (int k = 0; k < o->npar; k++) {
        const CalcGeomObj *p = &g->o[o->par[k]], *q = &g->o[o->par[(k + 1) % o->npar]];
        a += p->x * q->y - q->x * p->y;
    }
    return fabs(a) / 2;
}

static double poly_perimeter(const CalcGeom *g, const CalcGeomObj *o) {
    double s = 0;
    for (int k = 0; k < o->npar; k++) {
        const CalcGeomObj *p = &g->o[o->par[k]], *q = &g->o[o->par[(k + 1) % o->npar]];
        s += hypot(q->x - p->x, q->y - p->y);
    }
    return s;
}

static const char *const MEASURES[] = {"distance", "length", "slope",  "radius", "area",
                                       "perimeter", "angle", "xcoord", "ycoord"};
#define NMEASURES (int)(sizeof MEASURES / sizeof MEASURES[0])

static int measure_index(const char *p, size_t n) {
    for (int i = 0; i < NMEASURES; i++)
        if (strlen(MEASURES[i]) == n && !strncmp(MEASURES[i], p, n)) return i;
    return -1;
}

static int measure(const CalcGeom *g, int m, Span *args, int nargs, double *v, char *msg, size_t cap) {
    const char *fn = MEASURES[m];
    int ob[3];
    int want = m == 6 ? 3 : m == 0 ? 2 : 1;
    if (nargs != want) return fail(msg, cap, "%s: wrong number of parts", fn);
    for (int k = 0; k < nargs; k++) {
        ob[k] = obj_arg(g, args[k], g->n);
        if (ob[k] < 0) {
            char nm[40];
            snprintf(nm, sizeof nm, "%.*s", (int)(args[k].n < 30 ? args[k].n : 30), args[k].p);
            return fail(msg, cap, "%s is not a point, line or circle", nm);
        }
        if (!g->o[ob[k]].ok) return fail(msg, cap, "%s does not exist now", g->o[ob[k]].name);
    }
    const CalcGeomObj *a = &g->o[ob[0]], *b = nargs > 1 ? &g->o[ob[1]] : NULL;
#define WANT(cond, what)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) return fail(msg, cap, "%s: " what, fn);                                                           \
    } while (0)
    switch (m) {
    case 0: /* distance */
        WANT(a->kind == CALC_G_POINT, "measure from a point");
        if (b->kind == CALC_G_POINT) *v = hypot(b->x - a->x, b->y - a->y);
        else {
            WANT(b->kind == CALC_G_LINE, "to a point or a line");
            double dx = b->x2 - b->x, dy = b->y2 - b->y;
            *v = fabs((a->x - b->x) * dy - (a->y - b->y) * dx) / hypot(dx, dy);
        }
        break;
    case 1: /* length */
        WANT(a->kind == CALC_G_SEGMENT, "of a segment");
        *v = hypot(a->x2 - a->x, a->y2 - a->y);
        break;
    case 2: /* slope */
        WANT(linelike(a->kind), "of a line, segment or ray");
        *v = (a->y2 - a->y) / (a->x2 - a->x);
        break;
    case 3: /* radius */
        WANT(a->kind == CALC_G_CIRCLE, "of a circle");
        *v = a->r;
        break;
    case 4: /* area */
    case 5: /* perimeter */
        WANT(a->kind == CALC_G_CIRCLE || a->kind == CALC_G_POLYGON, "of a polygon or a circle");
        if (a->kind == CALC_G_CIRCLE) *v = m == 4 ? PI * a->r * a->r : 2 * PI * a->r;
        else *v = m == 4 ? poly_area(g, a) : poly_perimeter(g, a);
        break;
    case 6: { /* angle ABC, at B */
        const CalcGeomObj *c = &g->o[ob[2]];
        WANT(a->kind == CALC_G_POINT && b->kind == CALC_G_POINT && c->kind == CALC_G_POINT, "of three points");
        double ux = a->x - b->x, uy = a->y - b->y, wx = c->x - b->x, wy = c->y - b->y;
        WANT((ux || uy) && (wx || wy), "the corner is on an end");
        *v = fabs(atan2(ux * wy - uy * wx, ux * wx + uy * wy)) * 180 / PI;
        break;
    }
    default: /* xcoord, ycoord */
        WANT(a->kind == CALC_G_POINT, "of a point");
        *v = m == 7 ? a->x : a->y;
        break;
    }
#undef WANT
    return 0;
}

int calc_geom_needs(const char *text) {
    const char *p = text;
    while (*p) {
        if (!(isalpha((unsigned char)*p) || *p == '_') || (p > text && is_ident(p[-1]))) {
            p++;
            continue;
        }
        size_t n = 0;
        while (is_ident(p[n])) n++;
        const char *q = p + n;
        q += strspn(q, " ");
        if (*q == '(' && measure_index(p, n) >= 0) return 1;
        p += n;
    }
    return 0;
}

int calc_geom_expand(const CalcGeom *g, const char *in, char *out, size_t cap, char *msg, size_t msgcap) {
    size_t o = 0;
    if (msg && msgcap) msg[0] = 0;
    if (!cap) return -1;
    out[0] = 0;
    const char *p = in;
    while (*p) {
        size_t n = 0;
        int m = -1;
        if ((isalpha((unsigned char)*p) || *p == '_') && (p == in || !is_ident(p[-1]))) {
            while (is_ident(p[n])) n++;
            const char *q = p + n;
            q += strspn(q, " ");
            if (*q == '(' && (m = measure_index(p, n)) >= 0) {
                int depth = 0;
                const char *e = q;
                for (; *e; e++) {
                    if (*e == '(') depth++;
                    else if (*e == ')' && --depth == 0) break;
                }
                if (!*e) return fail(msg, msgcap, "%s( is not closed", MEASURES[m]);
                Span args[4];
                int nargs = split(q + 1, (size_t)(e - q - 1), args, 4);
                double v;
                if (nargs < 0) return fail(msg, msgcap, "%s: too many parts", MEASURES[m]);
                if (measure(g, m, args, nargs, &v, msg, msgcap)) return -1;
                char t[48];
                if (isfinite(v)) snprintf(t, sizeof t, "(%.17g)", v);
                else snprintf(t, sizeof t, "(1/0)"); /* a vertical line's slope */
                size_t tn = strlen(t);
                if (o + tn + 1 > cap) return fail(msg, msgcap, "too long", NULL);
                memcpy(out + o, t, tn + 1);
                o += tn;
                p = e + 1;
                continue;
            }
        }
        if (!n) n = 1;
        if (o + n + 1 > cap) return fail(msg, msgcap, "too long", NULL);
        memcpy(out + o, p, n);
        o += n;
        out[o] = 0;
        p += n;
    }
    return 0;
}

void calc_geom_describe(const CalcGeom *g, int i, char *out, size_t cap) {
    const CalcGeomObj *o = &g->o[i];
    char a[32], b[32], c[32];
    if (!o->ok) {
        snprintf(out, cap, "%s: %s", o->name, o->why);
        return;
    }
    switch (o->kind) {
    case CALC_G_POINT:
        sqm_format(o->x, 6, a, sizeof a);
        sqm_format(o->y, 6, b, sizeof b);
        snprintf(out, cap, "%s = (%s, %s)", o->name, a, b);
        break;
    case CALC_G_CIRCLE:
        sqm_format(o->x, 6, a, sizeof a);
        sqm_format(o->y, 6, b, sizeof b);
        sqm_format(o->r, 6, c, sizeof c);
        snprintf(out, cap, "%s: circle, centre (%s, %s), radius %s", o->name, a, b, c);
        break;
    case CALC_G_POLYGON:
        sqm_format(poly_area(g, o), 6, a, sizeof a);
        sqm_format(poly_perimeter(g, o), 6, b, sizeof b);
        snprintf(out, cap, "%s: polygon of %d corners, area %s, perimeter %s", o->name, o->npar, a, b);
        break;
    default: {
        double dx = o->x2 - o->x, dy = o->y2 - o->y;
        if (o->kind == CALC_G_SEGMENT) {
            sqm_format(hypot(dx, dy), 6, a, sizeof a);
            snprintf(out, cap, "%s: segment, length %s", o->name, a);
        } else if (dx == 0) {
            sqm_format(o->x, 6, a, sizeof a);
            snprintf(out, cap, "%s: %s x = %s", o->name, kind_name(o->kind), a);
        } else {
            double m = dy / dx, c0 = o->y - m * o->x;
            sqm_format(m, 6, a, sizeof a);
            sqm_format(fabs(c0), 6, b, sizeof b);
            if (!strcmp(b, "0")) snprintf(out, cap, "%s: %s y = %s x", o->name, kind_name(o->kind), a);
            else snprintf(out, cap, "%s: %s y = %s x %c %s", o->name, kind_name(o->kind), a, c0 < 0 ? '-' : '+', b);
        }
    }
    }
}

/* ---- drawing ---- */

#define COL_LINE 0xFF00707Au
#define COL_FILL 0x3000909Au
#define COL_POINT 0xFF203040u
#define COL_HOT 0xFFD06000u

static int clamp_px(double v) {
    if (!(v > -1e6)) return -1000000;
    if (v > 1e6) return 1000000;
    return (int)lround(v);
}

/* the part of the pixel line p + t d inside r, for t in [t0, t1]; 0 if none */
static int clip(sqg_rect r, double px, double py, double dx, double dy, double *t0, double *t1) {
    double lo[2] = {r.x - 2.0, r.y - 2.0}, hi[2] = {r.x + r.w + 2.0, r.y + r.h + 2.0}, p[2] = {px, py},
           d[2] = {dx, dy};
    for (int k = 0; k < 2; k++) {
        if (d[k] == 0) {
            if (p[k] < lo[k] || p[k] > hi[k]) return 0;
            continue;
        }
        double a = (lo[k] - p[k]) / d[k], b = (hi[k] - p[k]) / d[k];
        if (a > b) {
            double t = a;
            a = b;
            b = t;
        }
        if (a > *t0) *t0 = a;
        if (b < *t1) *t1 = b;
    }
    return *t0 <= *t1;
}

static void thick_line(sqg_canvas *c, double x0, double y0, double x1, double y1, uint32_t col) {
    int a = clamp_px(x0), b = clamp_px(y0), e = clamp_px(x1), f = clamp_px(y1);
    sqg_line(c, a, b, e, f, col);
    if (abs(e - a) >= abs(f - b)) sqg_line(c, a, b + 1, e, f + 1, col);
    else sqg_line(c, a + 1, b, e + 1, f, col);
}

static void draw_linelike(sqg_canvas *c, sqg_rect r, const CalcView *v, const CalcGeomObj *o, uint32_t col) {
    double ax = calc_px_x(v, r, o->x), ay = calc_px_y(v, r, o->y);
    double dx = calc_px_x(v, r, o->x2) - ax, dy = calc_px_y(v, r, o->y2) - ay;
    double t0 = o->kind == CALC_G_LINE ? -1e300 : 0, t1 = o->kind == CALC_G_SEGMENT ? 1 : 1e300;
    if (dx == 0 && dy == 0) return;
    if (!clip(r, ax, ay, dx, dy, &t0, &t1)) return;
    thick_line(c, ax + t0 * dx, ay + t0 * dy, ax + t1 * dx, ay + t1 * dy, col);
}

static void fill_polygon(sqg_canvas *c, sqg_rect r, const double *xs, const double *ys, int n, uint32_t argb) {
    double y0 = ys[0], y1 = ys[0];
    for (int k = 1; k < n; k++) {
        y0 = fmin(y0, ys[k]);
        y1 = fmax(y1, ys[k]);
    }
    int a = (int)fmax(ceil(y0 - 0.5), r.y), b = (int)fmin(floor(y1 - 0.5), r.y + r.h - 1);
    for (int y = a; y <= b; y++) {
        double cy = y + 0.5, cross[CALC_GEOM_POLY];
        int m = 0;
        for (int k = 0; k < n; k++) {
            double ax = xs[k], ay = ys[k], bx = xs[(k + 1) % n], by = ys[(k + 1) % n];
            if ((ay <= cy) != (by <= cy)) cross[m++] = ax + (cy - ay) / (by - ay) * (bx - ax);
        }
        for (int i = 1; i < m; i++) /* sort the few crossings */
            for (int j = i; j > 0 && cross[j - 1] > cross[j]; j--) {
                double t = cross[j];
                cross[j] = cross[j - 1];
                cross[j - 1] = t;
            }
        for (int i = 0; i + 1 < m; i += 2) {
            int x0 = (int)fmax(ceil(cross[i] - 0.5), r.x), x1 = (int)fmin(floor(cross[i + 1] - 0.5), r.x + r.w - 1);
            if (x1 >= x0) sqg_blend(c, sqg_rect_make(x0, y, x1 - x0 + 1, 1), argb);
        }
    }
}

int calc_geom_draw(sqg_canvas *c, sqg_rect r, const CalcView *v, const CalcGeom *g, int highlight) {
    if (r.w <= 0 || r.h <= 0) return 0;
    sqg_rect old = c->clip;
    sqg_clip(c, r);
    int drawn = 0;
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < g->n; i++) {
            const CalcGeomObj *o = &g->o[i];
            if (!o->ok) continue;
            int area = o->kind == CALC_G_POLYGON || o->kind == CALC_G_CIRCLE;
            if ((pass == 0) != area || (pass == 2) != (o->kind == CALC_G_POINT)) continue;
            drawn++;
            if (o->kind == CALC_G_POLYGON) {
                double xs[CALC_GEOM_POLY] = {0}, ys[CALC_GEOM_POLY] = {0};
                int near = 1;
                for (int k = 0; k < o->npar; k++) {
                    xs[k] = calc_px_x(v, r, g->o[o->par[k]].x);
                    ys[k] = calc_px_y(v, r, g->o[o->par[k]].y);
                    near &= fabs(xs[k]) < 1e6 && fabs(ys[k]) < 1e6;
                }
                if (near) fill_polygon(c, r, xs, ys, o->npar, COL_FILL);
                for (int k = 0; k < o->npar; k++) {
                    CalcGeomObj e = {0};
                    e.kind = CALC_G_SEGMENT;
                    e.x = g->o[o->par[k]].x;
                    e.y = g->o[o->par[k]].y;
                    e.x2 = g->o[o->par[(k + 1) % o->npar]].x;
                    e.y2 = g->o[o->par[(k + 1) % o->npar]].y;
                    draw_linelike(c, r, v, &e, COL_LINE);
                }
            } else if (o->kind == CALC_G_CIRCLE) {
                double cx = calc_px_x(v, r, o->x), cy = calc_px_y(v, r, o->y);
                double rx = fabs(calc_px_x(v, r, o->x + o->r) - cx), ry = fabs(calc_px_y(v, r, o->y + o->r) - cy);
                int steps = (int)fmin(2048, fmax(24, 2 * (rx + ry)));
                double px = cx + rx, py = cy;
                for (int k = 1; k <= steps; k++) {
                    double t = 2 * PI * k / steps, qx = cx + rx * cos(t), qy = cy - ry * sin(t);
                    int in = !((px < r.x - 4 && qx < r.x - 4) || (px > r.x + r.w + 4 && qx > r.x + r.w + 4) ||
                               (py < r.y - 4 && qy < r.y - 4) || (py > r.y + r.h + 4 && qy > r.y + r.h + 4));
                    if (in) thick_line(c, px, py, qx, qy, COL_LINE);
                    px = qx;
                    py = qy;
                }
            } else if (o->kind == CALC_G_POINT) {
                double px = calc_px_x(v, r, o->x), py = calc_px_y(v, r, o->y);
                if (!(px > r.x - 10 && px < r.x + r.w + 10 && py > r.y - 10 && py < r.y + r.h + 10)) continue;
                int x = (int)lround(px), y = (int)lround(py), rad = i == highlight ? 5 : 3;
                uint32_t col = i == highlight ? COL_HOT : COL_POINT;
                for (int dy = -rad; dy <= rad; dy++)
                    for (int dx = -rad; dx <= rad; dx++)
                        if (dx * dx + dy * dy <= rad * rad + rad) sqg_pixel(c, x + dx, y + dy, col);
                if (calc_geom_draggable(g, i)) /* a hollow centre says it can be dragged */
                    sqg_pixel(c, x, y, 0xFFFFFFFFu);
                sqg_text(c, x + 6, y - 12, o->name, col, 1);
            } else {
                draw_linelike(c, r, v, o, COL_LINE);
            }
        }
    c->clip = old;
    return drawn;
}

int calc_geom_hit(const CalcGeom *g, sqg_rect r, const CalcView *v, double px, double py, double tol) {
    int best = -1;
    double bd = tol;
    for (int i = 0; i < g->n; i++) {
        if (!g->o[i].ok || !calc_geom_draggable(g, i)) continue;
        double d = hypot(calc_px_x(v, r, g->o[i].x) - px, calc_px_y(v, r, g->o[i].y) - py);
        if (d <= bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}
