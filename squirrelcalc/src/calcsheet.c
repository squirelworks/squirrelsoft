/* calcsheet: the spreadsheet and the extra functions (see calcsheet.h). Original code. */
#include "calcsheet.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calcstats.h"
#include "sqmath.h"

#define MAX_DEPTH 120      /* cells waiting on cells, and nested functions */
#define MAX_VALUES (CALC_SHEET_ROWS * CALC_SHEET_COLS)
#define BUF 512

static SqmExpr scratch; /* parsed and evaluated at once, so one is enough */

typedef struct {
    CalcSheet *s;  /* NULL: no cells */
    int live;      /* 1 while recalculating: a cell is worked out when it is named */
    const CalcEnv *env;
    char *msg;
    size_t msgcap;
    int depth;
} Ctx;

static int fail(Ctx *x, const char *fmt, const char *a) {
    if (x->msg && x->msgcap) snprintf(x->msg, x->msgcap, fmt, a ? a : "");
    return -1;
}

static int is_ident(int ch) { return isalnum((unsigned char)ch) || ch == '_'; }

/* ---- cells ---- */

void calc_sheet_init(CalcSheet *s) {
    memset(s, 0, sizeof *s);
    for (int r = 0; r < CALC_SHEET_ROWS; r++)
        for (int c = 0; c < CALC_SHEET_COLS; c++) s->val[r][c] = NAN;
}

int calc_cell_parse(const char *t, int *row, int *col, int *abs) {
    int a = 0, k = 0;
    if (t[k] == '$') {
        a |= 1;
        k++;
    }
    if (t[k] < 'A' || t[k] >= 'A' + CALC_SHEET_COLS) return 0;
    int c = t[k++] - 'A';
    if (t[k] == '$') {
        a |= 2;
        k++;
    }
    if (!isdigit((unsigned char)t[k]) || t[k] == '0') return 0;
    int r = 0;
    while (isdigit((unsigned char)t[k]) && r <= CALC_SHEET_ROWS) r = r * 10 + (t[k++] - '0');
    if (r < 1 || r > CALC_SHEET_ROWS || is_ident(t[k])) return 0;
    *row = r - 1;
    *col = c;
    if (abs) *abs = a;
    return k;
}

void calc_cell_name(int row, int col, char *out, size_t cap) { snprintf(out, cap, "%c%d", 'A' + col, row + 1); }

static int on_sheet(int r, int c) { return r >= 0 && r < CALC_SHEET_ROWS && c >= 0 && c < CALC_SHEET_COLS; }

int calc_sheet_set(CalcSheet *s, int row, int col, const char *text) {
    if (!on_sheet(row, col)) return -1;
    if (!text) text = "";
    if (strlen(text) >= CALC_CELL_LEN) return -1;
    snprintf(s->text[row][col], CALC_CELL_LEN, "%s", text);
    const char *p = text + strspn(text, " ");
    s->bad[row][col] = 0;
    s->err[row][col][0] = 0;
    s->val[row][col] = NAN;
    if (!*p) {
        s->kind[row][col] = CALC_CELL_EMPTY;
    } else if (*p == '=') {
        s->kind[row][col] = CALC_CELL_FORMULA;
    } else {
        char *end;
        double v = strtod(p, &end);
        end += strspn(end, " ");
        int num = end != p && !*end && isfinite(v) && !isalpha((unsigned char)*p);
        s->kind[row][col] = num ? CALC_CELL_NUMBER : CALC_CELL_TEXT;
        if (num) s->val[row][col] = v;
    }
    return 0;
}

static int expand(Ctx *x, const char *in, char *out, size_t cap);

static void eval_cell(Ctx *x, int r, int c) {
    CalcSheet *s = x->s;
    if (s->kind[r][c] != CALC_CELL_FORMULA || s->mark[r][c] == 2) return;
    char name[16];
    calc_cell_name(r, c, name, sizeof name);
    if (s->mark[r][c] == 1) { /* named again while it is being worked out */
        s->bad[r][c] = 1;
        snprintf(s->err[r][c], sizeof s->err[r][c], "a cycle through %s", name);
        return;
    }
    if (x->depth >= MAX_DEPTH) {
        s->bad[r][c] = 1;
        snprintf(s->err[r][c], sizeof s->err[r][c], "cells nested too deep");
        return;
    }
    s->mark[r][c] = 1;
    x->depth++;
    char buf[BUF], why[40] = "";
    char *save_msg = x->msg;
    size_t save_cap = x->msgcap;
    x->msg = why;
    x->msgcap = sizeof why;
    const char *text = s->text[r][c] + strspn(s->text[r][c], " ") + 1;
    double v = NAN;
    int ok = expand(x, text, buf, sizeof buf) == 0;
    if (ok) {
        SqmError e;
        if (sqm_parse(buf, &scratch, &e) != SQM_OK) {
            snprintf(why, sizeof why, "%.39s", !*text ? "an empty formula" : e.msg);
            ok = 0;
        } else {
            const CalcEnv *env = x->env;
            if (sqm_eval(&scratch, env ? env->names : NULL, env ? env->values : NULL, env ? env->n : 0, &v) != SQM_OK) {
                const char *missing = "?";
                for (int k = 0; k < scratch.nnames; k++) {
                    int known = 0;
                    for (int j = 0; env && j < env->n; j++) known |= !strcmp(env->names[j], scratch.names[k]);
                    if (!known) missing = scratch.names[k];
                }
                snprintf(why, sizeof why, "define %.15s first", missing);
                ok = 0;
            }
        }
    }
    x->msg = save_msg;
    x->msgcap = save_cap;
    x->depth--;
    if (s->bad[r][c]) { /* a cycle came back to this cell */
        s->val[r][c] = NAN;
    } else if (ok) {
        s->val[r][c] = v;
    } else {
        s->bad[r][c] = 1;
        s->val[r][c] = NAN;
        snprintf(s->err[r][c], sizeof s->err[r][c], "%s", why[0] ? why : "error");
    }
    s->mark[r][c] = 2;
}

void calc_sheet_recalc(CalcSheet *s, const CalcEnv *env) {
    for (int r = 0; r < CALC_SHEET_ROWS; r++)
        for (int c = 0; c < CALC_SHEET_COLS; c++) {
            s->mark[r][c] = 0;
            if (s->kind[r][c] == CALC_CELL_FORMULA) {
                s->bad[r][c] = 0;
                s->err[r][c][0] = 0;
                s->val[r][c] = NAN;
            }
        }
    Ctx x = {s, 1, env, NULL, 0, 0};
    for (int r = 0; r < CALC_SHEET_ROWS; r++)
        for (int c = 0; c < CALC_SHEET_COLS; c++) eval_cell(&x, r, c);
}

/* the value a formula sees for a cell: 0 when empty */
static int cell_value(Ctx *x, int r, int c, double *v) {
    CalcSheet *s = x->s;
    char name[16];
    calc_cell_name(r, c, name, sizeof name);
    if (x->live) eval_cell(x, r, c);
    switch (s->kind[r][c]) {
    case CALC_CELL_EMPTY: *v = 0; return 0;
    case CALC_CELL_TEXT: return fail(x, "%s is text", name);
    default:
        if (s->bad[r][c]) return fail(x, s->mark[r][c] == 1 ? "a cycle through %s" : "%s has an error", name);
        *v = s->val[r][c];
        return 0;
    }
}

/* "A1:B5", "B:B", "A:C" or one cell: the rectangle, rows and columns in order */
static int parse_range(const char *t, int *r0, int *c0, int *r1, int *c1) {
    t += strspn(t, " ");
    int n = calc_cell_parse(t, r0, c0, NULL);
    if (n) {
        const char *q = t + n;
        q += strspn(q, " ");
        if (*q == ':') {
            q++;
            q += strspn(q, " ");
            int m = calc_cell_parse(q, r1, c1, NULL);
            if (!m) return -1;
            q += m;
        } else {
            *r1 = *r0;
            *c1 = *c0;
        }
        if (q[strspn(q, " ")]) return -1;
    } else {
        /* whole columns */
        if (t[0] < 'A' || t[0] >= 'A' + CALC_SHEET_COLS) return -1;
        const char *q = t + 1;
        q += strspn(q, " ");
        if (*q != ':') return -1;
        q++;
        q += strspn(q, " ");
        if (q[0] < 'A' || q[0] >= 'A' + CALC_SHEET_COLS || is_ident(q[1])) return -1;
        if (q[1 + strspn(q + 1, " ")]) return -1;
        *c0 = t[0] - 'A';
        *c1 = q[0] - 'A';
        *r0 = 0;
        *r1 = CALC_SHEET_ROWS - 1;
    }
    if (*r0 > *r1) {
        int k = *r0;
        *r0 = *r1;
        *r1 = k;
    }
    if (*c0 > *c1) {
        int k = *c0;
        *c0 = *c1;
        *c1 = k;
    }
    return 0;
}

/* the numbers in a range; -2 when text is not a range, -1 on an error cell */
static int range_values(Ctx *x, const char *text, double *out, int cap) {
    int r0, c0, r1, c1;
    if (!x->s || parse_range(text, &r0, &c0, &r1, &c1)) return -2;
    int n = 0;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            if (x->live) eval_cell(x, r, c);
            int k = x->s->kind[r][c];
            if (k == CALC_CELL_EMPTY || k == CALC_CELL_TEXT) continue;
            if (x->s->bad[r][c]) {
                char name[16];
                calc_cell_name(r, c, name, sizeof name);
                return fail(x, "%s has an error", name);
            }
            if (n < cap) out[n] = x->s->val[r][c];
            n++;
        }
    return n < cap ? n : cap;
}

int calc_sheet_values(const CalcSheet *s, const char *range, double *out, int cap) {
    Ctx x = {(CalcSheet *)s, 0, NULL, NULL, 0, 0};
    int n = range_values(&x, range, out, cap);
    return n == -2 ? -1 : n < 0 ? 0 : n;
}

/* ---- the extra functions ---- */

/* a whole number in text: digits, a point, an exponent */
static size_t number_len(const char *p) {
    size_t k = 0;
    while (isdigit((unsigned char)p[k]) || p[k] == '.') k++;
    if ((p[k] == 'e' || p[k] == 'E') && k > 0) {
        size_t j = k + 1;
        if (p[j] == '+' || p[j] == '-') j++;
        if (isdigit((unsigned char)p[j])) {
            while (isdigit((unsigned char)p[j])) j++;
            k = j;
        }
    }
    return k;
}

static int emit(Ctx *x, char *out, size_t cap, size_t *o, const char *text, size_t n) {
    if (*o + n + 1 > cap) return fail(x, "the formula gets too long", NULL);
    memcpy(out + *o, text, n);
    *o += n;
    out[*o] = 0;
    return 0;
}

static int emit_num(Ctx *x, char *out, size_t cap, size_t *o, double v) {
    char t[48];
    if (isnan(v)) snprintf(t, sizeof t, "(0/0)");
    else if (isinf(v)) snprintf(t, sizeof t, v > 0 ? "(1/0)" : "(-1/0)");
    else snprintf(t, sizeof t, "(%.17g)", v);
    return emit(x, out, cap, o, t, strlen(t));
}

/* the value of an expression: cells, functions and the variables */
static int number_of(Ctx *x, const char *text, double *v) {
    char buf[BUF];
    if (expand(x, text, buf, sizeof buf)) return -1;
    SqmError e;
    if (sqm_parse(buf, &scratch, &e) != SQM_OK) return fail(x, "%s", e.msg);
    const CalcEnv *env = x->env;
    if (sqm_eval(&scratch, env ? env->names : NULL, env ? env->values : NULL, env ? env->n : 0, v) != SQM_OK) {
        for (int k = 0; k < scratch.nnames; k++) {
            int known = 0;
            for (int j = 0; env && j < env->n; j++) known |= !strcmp(env->names[j], scratch.names[k]);
            if (!known) return fail(x, "%s has no value here", scratch.names[k]);
        }
        return fail(x, "no value", NULL);
    }
    return 0;
}

enum { F_SUM, F_MEAN, F_MEDIAN, F_SD, F_VAR, F_COUNT, F_MIN, F_MAX, F_Q1, F_Q3, /* over values */
       F_NPDF, F_NCDF, F_INVN, F_BPDF, F_BCDF, F_PPDF, F_PCDF, F_NCR, F_NPR };

static const struct {
    const char *name;
    int fn, lo, hi; /* argument counts, for the numeric ones */
} FUNCS[] = {
    {"sum", F_SUM, 1, 32},       {"mean", F_MEAN, 1, 32},     {"median", F_MEDIAN, 1, 32},
    {"sd", F_SD, 1, 32},         {"var", F_VAR, 1, 32},       {"count", F_COUNT, 1, 32},
    {"min", F_MIN, 1, 32},       {"max", F_MAX, 1, 32},       {"q1", F_Q1, 1, 32},
    {"q3", F_Q3, 1, 32},         {"normalpdf", F_NPDF, 1, 3}, {"normalcdf", F_NCDF, 2, 4},
    {"invnorm", F_INVN, 1, 3},   {"binompdf", F_BPDF, 3, 3},  {"binomcdf", F_BCDF, 3, 3},
    {"poissonpdf", F_PPDF, 2, 2}, {"poissoncdf", F_PCDF, 2, 2}, {"ncr", F_NCR, 2, 2},
    {"npr", F_NPR, 2, 2},
};
#define NFUNCS (int)(sizeof FUNCS / sizeof FUNCS[0])

/* in any case: =SUM(A1:A3) as spreadsheets write it */
static int find_func(const char *name, size_t len) {
    for (int i = 0; i < NFUNCS; i++) {
        if (strlen(FUNCS[i].name) != len) continue;
        size_t k = 0;
        while (k < len && tolower((unsigned char)name[k]) == FUNCS[i].name[k]) k++;
        if (k == len) return i;
    }
    return -1;
}

#define MAX_ARGS 32

typedef struct {
    const char *p;
    size_t n;
} Span;

/* split "a, b(c, d), e" at its top-level commas */
static int split_args(Ctx *x, const char *p, size_t len, Span *args, int cap) {
    int n = 0, depth = 0;
    size_t start = 0;
    for (size_t k = 0; k <= len; k++) {
        if (k < len && (p[k] == '(' || p[k] == '[' || p[k] == '{')) depth++;
        if (k < len && (p[k] == ')' || p[k] == ']' || p[k] == '}')) depth--;
        if (k == len || (p[k] == ',' && depth == 0)) {
            if (n >= cap) return fail(x, "too many arguments", NULL);
            size_t m = k - start;
            if (m >= BUF) return fail(x, "an argument is too long", NULL);
            size_t sp = 0;
            while (sp < m && p[start + sp] == ' ') sp++;
            if (sp == m) return fail(x, "an argument is missing", NULL);
            args[n].p = p + start;
            args[n].n = m;
            n++;
            start = k + 1;
        }
    }
    return n;
}

static void arg_text(Span a, char *out) {
    memcpy(out, a.p, a.n);
    out[a.n] = 0;
}

/* a list of the calculator's, by its name alone; -2 when it is not one */
static int list_values(Ctx *x, const char *text, double *out, int cap) {
    const char *p = text + strspn(text, " ");
    size_t n = 0;
    while (is_ident(p[n])) n++;
    if (!n || isdigit((unsigned char)p[0]) || p[n + strspn(p + n, " ")] || n >= 32 || !x->env || !x->env->list)
        return -2;
    char name[32];
    memcpy(name, p, n);
    name[n] = 0;
    int k = x->env->list(x->env->ctx, name, out, cap);
    return k < 0 ? -2 : k;
}

static int is_values_arg(Ctx *x, const char *a) {
    int r0, c0, r1, c1;
    if (x->s && !parse_range(a, &r0, &c0, &r1, &c1) && strchr(a, ':')) return 1;
    double tmp[1];
    return list_values(x, a, tmp, 0) != -2;
}

/* sum, mean ... over ranges, lists and single values */
static int aggregate(Ctx *x, int f, const Span *args, int nargs, char *out, size_t cap, size_t *o) {
    const char *name = FUNCS[f].name;
    int fn = FUNCS[f].fn;
    double v[MAX_VALUES];
    char a[BUF];
    int n = 0;
    for (int i = 0; i < nargs; i++) {
        arg_text(args[i], a);
        int got = range_values(x, a, v + n, MAX_VALUES - n);
        if (got == -2) got = list_values(x, a, v + n, MAX_VALUES - n);
        if (got == -1) return -1;
        if (got == -2) {
            if (n >= MAX_VALUES) return fail(x, "%s: too many values", name);
            if (number_of(x, a, v + n)) return -1;
            got = 1;
        }
        n += got;
    }
    if (fn == F_COUNT) return emit_num(x, out, cap, o, n);
    if (fn == F_SUM) {
        double s = 0;
        for (int i = 0; i < n; i++) s += v[i];
        return emit_num(x, out, cap, o, s);
    }
    if (n == 0) return fail(x, "%s: no values", name);
    double r;
    if (fn == F_MIN || fn == F_MAX || fn == F_MEAN) {
        r = v[0];
        double s = 0;
        for (int i = 0; i < n; i++) {
            if (fn == F_MIN && v[i] < r) r = v[i];
            if (fn == F_MAX && v[i] > r) r = v[i];
            s += v[i];
        }
        if (fn == F_MEAN) r = s / n;
        return emit_num(x, out, cap, o, r);
    }
    if (n > CALC_LIST_MAX) return fail(x, "%s: at most 100 values", name);
    CalcStats st;
    if (calc_stats(v, n, &st)) return fail(x, "%s: a value is not a number", name);
    r = fn == F_MEDIAN ? st.median : fn == F_SD ? st.sd : fn == F_VAR ? st.var : fn == F_Q1 ? st.q1 : st.q3;
    return emit_num(x, out, cap, o, r);
}

static int call(Ctx *x, int f, const Span *args, int nargs, char *out, size_t cap, size_t *o) {
    const char *name = FUNCS[f].name;
    int fn = FUNCS[f].fn;
    if (nargs < FUNCS[f].lo || nargs > FUNCS[f].hi) return fail(x, "%s: wrong number of arguments", name);
    char a[3][BUF];
    if (fn <= F_Q3) {
        if ((fn == F_MIN || fn == F_MAX) && nargs == 2) {
            arg_text(args[0], a[0]);
            arg_text(args[1], a[1]);
            if (!is_values_arg(x, a[0]) && !is_values_arg(x, a[1])) {
                /* sqmath's own min and max: these may depend on x */
                char b[2][BUF];
                if (expand(x, a[0], b[0], sizeof b[0]) || expand(x, a[1], b[1], sizeof b[1])) return -1;
                char t[2 * BUF + 16];
                snprintf(t, sizeof t, "%s((%s), (%s))", name, b[0], b[1]);
                return emit(x, out, cap, o, t, strlen(t));
            }
        }
        return aggregate(x, f, args, nargs, out, cap, o);
    }
    if (fn == F_NPDF) { /* written out, so it can graph */
        const char *def[3] = {"0", "0", "1"};
        if (nargs == 2) return fail(x, "normalpdf(x) or normalpdf(x, mu, sd)", NULL);
        for (int i = 0; i < 3; i++) {
            if (i < nargs) {
                char t[BUF];
                arg_text(args[i], t);
                if (expand(x, t, a[i], sizeof a[i])) return -1;
            } else {
                snprintf(a[i], sizeof a[i], "%s", def[i]);
            }
        }
        char t[4 * BUF + 96];
        snprintf(t, sizeof t, "(exp(-(((%s) - (%s))/(%s))^2/2)/((%s)*2.5066282746310002))", a[0], a[1], a[2], a[2]);
        return emit(x, out, cap, o, t, strlen(t));
    }
    double v[4] = {0, 0, 0, 1};
    for (int i = 0; i < nargs; i++) {
        arg_text(args[i], a[0]);
        if (number_of(x, a[0], &v[i])) return -1;
    }
    double r;
    switch (fn) {
    case F_NCDF:
        if (nargs == 3) return fail(x, "normalcdf(lo, hi) or normalcdf(lo, hi, mu, sd)", NULL);
        r = calc_normal_cdf(v[0], v[1], nargs == 4 ? v[2] : 0, nargs == 4 ? v[3] : 1);
        break;
    case F_INVN:
        if (nargs == 2) return fail(x, "invnorm(p) or invnorm(p, mu, sd)", NULL);
        if (!(v[0] > 0 && v[0] < 1)) return fail(x, "invnorm: p is between 0 and 1", NULL);
        r = calc_inv_norm(v[0], nargs == 3 ? v[1] : 0, nargs == 3 ? v[2] : 1);
        break;
    case F_BPDF: r = calc_binom_pdf(v[0], v[1], v[2]); break;
    case F_BCDF: r = calc_binom_cdf(v[0], v[1], v[2]); break;
    case F_PPDF: r = calc_poisson_pdf(v[0], v[1]); break;
    case F_PCDF: r = calc_poisson_cdf(v[0], v[1]); break;
    case F_NCR: r = calc_ncr(v[0], v[1]); break;
    default: r = calc_npr(v[0], v[1]); break;
    }
    if (isnan(r)) return fail(x, "%s: these values are out of its range", name);
    return emit_num(x, out, cap, o, r);
}

static int expand(Ctx *x, const char *in, char *out, size_t cap) {
    size_t o = 0;
    if (cap) out[0] = 0;
    if (x->depth >= MAX_DEPTH) return fail(x, "nested too deep", NULL);
    x->depth++;
    int rc = 0;
    const char *p = in;
    while (*p && rc == 0) {
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            size_t n = number_len(p);
            rc = emit(x, out, cap, &o, p, n);
            p += n;
            continue;
        }
        if (!(isalpha((unsigned char)*p) || *p == '_' || *p == '$')) {
            rc = emit(x, out, cap, &o, p, 1);
            p++;
            continue;
        }
        int r, c;
        int n = x->s ? calc_cell_parse(p, &r, &c, NULL) : 0;
        if (n) {
            const char *q = p + n;
            q += strspn(q, " ");
            if (*q == ':') {
                rc = fail(x, "a range goes inside sum(), mean() or another function of values", NULL);
                break;
            }
            double v;
            rc = cell_value(x, r, c, &v);
            if (rc == 0) rc = emit_num(x, out, cap, &o, v);
            p += n;
            continue;
        }
        size_t len = 0;
        if (*p == '$') {
            rc = emit(x, out, cap, &o, p, 1);
            p++;
            continue;
        }
        while (is_ident(p[len])) len++;
        const char *q = p + len;
        q += strspn(q, " ");
        int f = find_func(p, len);
        if (f >= 0 && *q == '(') {
            int depth = 0;
            const char *e = q;
            for (; *e; e++) {
                if (*e == '(') depth++;
                else if (*e == ')' && --depth == 0) break;
            }
            if (!*e) {
                rc = fail(x, "%s( is not closed", FUNCS[f].name);
                break;
            }
            Span a[MAX_ARGS];
            int nargs = split_args(x, q + 1, (size_t)(e - q - 1), a, MAX_ARGS);
            if (nargs < 0) {
                rc = -1;
                break;
            }
            rc = call(x, f, a, nargs, out, cap, &o);
            p = e + 1;
            continue;
        }
        rc = emit(x, out, cap, &o, p, len);
        p += len;
    }
    x->depth--;
    return rc;
}

int calc_expand(const char *in, char *out, size_t cap, const CalcSheet *s, const CalcEnv *env, char *msg,
                size_t msgcap) {
    Ctx x = {(CalcSheet *)s, 0, env, msg, msgcap, 0};
    if (msg && msgcap) msg[0] = 0;
    return expand(&x, in, out, cap);
}

int calc_needs_expand(const char *text) {
    const char *p = text;
    while (*p) {
        if (isdigit((unsigned char)*p) || *p == '.') {
            size_t n = number_len(p);
            p += n ? n : 1;
            continue;
        }
        if (!(isalpha((unsigned char)*p) || *p == '_' || *p == '$')) {
            p++;
            continue;
        }
        int r, c;
        if (calc_cell_parse(p, &r, &c, NULL)) return 1;
        size_t len = 0;
        while (is_ident(p[len])) len++;
        if (!len) len = 1;
        const char *q = p + len;
        q += strspn(q, " ");
        if (*q == '(' && find_func(p, len) >= 0) return 1;
        p += len;
    }
    return 0;
}

/* ---- copying ---- */

int calc_shift_refs(const char *in, int dr, int dc, char *out, size_t cap) {
    size_t o = 0;
    int bad = 0;
    const char *p = in;
    if (cap) out[0] = 0;
#define PUT(s, n)                                                                                                      \
    do {                                                                                                               \
        size_t n_ = (n);                                                                                               \
        if (o + n_ + 1 > cap) return -1;                                                                               \
        memcpy(out + o, (s), n_);                                                                                      \
        o += n_;                                                                                                       \
        out[o] = 0;                                                                                                    \
    } while (0)
    while (*p) {
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
            size_t n = number_len(p);
            PUT(p, n);
            p += n;
            continue;
        }
        if (!(isalpha((unsigned char)*p) || *p == '_' || *p == '$')) {
            PUT(p, 1);
            p++;
            continue;
        }
        int r, c, abs;
        int n = calc_cell_parse(p, &r, &c, &abs);
        if (n) {
            if (!(abs & 1)) c += dc;
            if (!(abs & 2)) r += dr;
            char t[16];
            if (!on_sheet(r, c)) {
                snprintf(t, sizeof t, "#REF");
                bad = 1;
            } else {
                snprintf(t, sizeof t, "%s%c%s%d", abs & 1 ? "$" : "", 'A' + c, abs & 2 ? "$" : "", r + 1);
            }
            PUT(t, strlen(t));
            p += n;
            continue;
        }
        /* whole columns: B:B */
        if (p[0] >= 'A' && p[0] < 'A' + CALC_SHEET_COLS && p[1] == ':' && p[2] >= 'A' &&
            p[2] < 'A' + CALC_SHEET_COLS && !is_ident(p[3])) {
            int a = p[0] - 'A' + dc, b = p[2] - 'A' + dc;
            char t[4] = {(char)('A' + a), ':', (char)('A' + b), 0};
            if (a < 0 || b < 0 || a >= CALC_SHEET_COLS || b >= CALC_SHEET_COLS) {
                PUT("#REF", 4);
                bad = 1;
            } else {
                PUT(t, 3);
            }
            p += 3;
            continue;
        }
        size_t len = 0;
        if (*p == '$') len = 1;
        while (is_ident(p[len])) len++;
        PUT(p, len);
        p += len;
    }
#undef PUT
    return bad ? -1 : 0;
}

int calc_sheet_copy(CalcSheet *s, int r0, int c0, int r1, int c1) {
    if (!on_sheet(r0, c0) || !on_sheet(r1, c1)) return -1;
    char t[CALC_CELL_LEN + 64];
    const char *from = s->text[r0][c0];
    int rc = 0;
    if (s->kind[r0][c0] == CALC_CELL_FORMULA) rc = calc_shift_refs(from, r1 - r0, c1 - c0, t, sizeof t);
    else snprintf(t, sizeof t, "%s", from);
    if (strlen(t) >= CALC_CELL_LEN) return -1;
    calc_sheet_set(s, r1, c1, t);
    return rc;
}

/* ---- CSV ---- */

int calc_sheet_load_csv(CalcSheet *s, const char *csv) {
    calc_sheet_init(s);
    int r = 0, c = 0, set = 0, lost = 0;
    const char *p = csv;
    while (*p) {
        char field[CALC_CELL_LEN * 2];
        size_t n = 0;
        int too_long = 0;
        if (*p == '"') {
            p++;
            while (*p) {
                if (*p == '"' && p[1] == '"') {
                    if (n + 1 < sizeof field) field[n++] = '"';
                    else too_long = 1;
                    p += 2;
                } else if (*p == '"') {
                    p++;
                    break;
                } else {
                    if (n + 1 < sizeof field) field[n++] = *p;
                    else too_long = 1;
                    p++;
                }
            }
            while (*p && *p != ',' && *p != '\n' && *p != '\r') p++; /* anything after the quote */
        } else {
            while (*p && *p != ',' && *p != '\n' && *p != '\r') {
                if (n + 1 < sizeof field) field[n++] = *p;
                else too_long = 1;
                p++;
            }
        }
        field[n] = 0;
        if (n) {
            if (on_sheet(r, c) && !too_long && calc_sheet_set(s, r, c, field) == 0) set++;
            else lost = 1;
        }
        if (*p == ',') {
            p++;
            c++;
        } else {
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            r++;
            c = 0;
        }
    }
    return lost ? -1 : set;
}

size_t calc_sheet_save_csv(const CalcSheet *s, int formulas, char *out, size_t cap) {
    int rows = 0, cols = 0;
    for (int r = 0; r < CALC_SHEET_ROWS; r++)
        for (int c = 0; c < CALC_SHEET_COLS; c++)
            if (s->kind[r][c] != CALC_CELL_EMPTY) {
                if (r + 1 > rows) rows = r + 1;
                if (c + 1 > cols) cols = c + 1;
            }
    size_t at = 0;
    if (cap) out[0] = 0;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            char t[CALC_CELL_LEN + 8];
            int k = s->kind[r][c];
            if (formulas || k == CALC_CELL_TEXT) snprintf(t, sizeof t, "%s", s->text[r][c]);
            else if (k == CALC_CELL_EMPTY) t[0] = 0;
            else if (s->bad[r][c]) snprintf(t, sizeof t, "#ERR");
            else sqm_format(s->val[r][c], 15, t, sizeof t);
            char q[2 * sizeof t + 4];
            size_t m = 0;
            if (strpbrk(t, ",\"\n\r")) {
                q[m++] = '"';
                for (const char *e = t; *e; e++) {
                    if (*e == '"') q[m++] = '"';
                    q[m++] = *e;
                }
                q[m++] = '"';
                q[m] = 0;
            } else {
                m = (size_t)snprintf(q, sizeof q, "%s", t);
            }
            if (c) at += (size_t)snprintf(at < cap ? out + at : NULL, at < cap ? cap - at : 0, ",");
            at += (size_t)snprintf(at < cap ? out + at : NULL, at < cap ? cap - at : 0, "%s", q);
        }
        at += (size_t)snprintf(at < cap ? out + at : NULL, at < cap ? cap - at : 0, "\n");
    }
    return at;
}
