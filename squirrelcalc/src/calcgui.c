/* calcgui: the SquirrelCalc window panel (see calcgui.h). Original code. */
#include "calcgui.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const uint32_t COLORS[CALC_N] = {0xFF2060D0u, 0xFFD03030u, 0xFF20A040u, 0xFF9040C0u};
#define COL_DERIV 0xFF80A8F0u
#define COL_ERR 0xFFB02020u
#define COL_FIT 0xFFE07000u
#define COL_POINTS 0xFF303030u

static SqmExpr scratch, scratch2; /* big: kept off the stack */

#define HINT "Type a function of x (x^2 - 2), click a curve to trace it, integrate y1 from 0 to 2"

/* the pair index of yi and yj, i < j: (0,1) (0,2) (0,3) (1,2) (1,3) (2,3) */
static void pair_of(int k, int *i, int *j) {
    static const int P[CALC_PAIRS][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    *i = P[k][0];
    *j = P[k][1];
}

void calc_gui_init(CalcGui *g) {
    memset(g, 0, sizeof *g);
    g->trace_fn = -1;
    g->area_fn = -1;
    snprintf(g->tstart, sizeof g->tstart, "-5");
    snprintf(g->tstep, sizeof g->tstep, "1");
    snprintf(g->status, sizeof g->status, "%s", HINT);
}

double calc_gui_var(const CalcGui *g, const char *name) {
    for (int i = 0; i < g->nvars; i++)
        if (!strcmp(g->vname[i], name)) return g->vval[i];
    return NAN;
}

/* slots: x, then the user variables */
static int slots(const CalcGui *g, const char **names, double *values) {
    names[0] = "x";
    values[0] = 0;
    for (int i = 0; i < g->nvars; i++) {
        names[i + 1] = g->vname[i];
        values[i + 1] = g->vval[i];
    }
    return g->nvars + 1;
}

static void compile_deriv(CalcGui *g) {
    g->dok = 0;
    g->dtext[0] = 0;
    if (!g->ok[0] || g->kind[0] != CALC_FUNC || sqm_parse(g->fx[0] + g->body[0], &scratch, NULL) != SQM_OK) return;
    int rc = sqm_diff(&scratch, "x", &scratch2);
    if (rc != SQM_OK) {
        snprintf(g->dtext, sizeof g->dtext, "d/dx y1: %s", sqm_strerror(rc));
        return;
    }
    char t[180];
    sqm_print(&scratch2, t, sizeof t);
    snprintf(g->dtext, sizeof g->dtext, "y1' = %s", t);
    const char *names[CALC_VARS + 1];
    double values[CALC_VARS + 1];
    int n = slots(g, names, values);
    g->dok = sqm_compile(&scratch2, names, n, &g->dprog) == SQM_OK;
}

/* yi - yj for every pair that both draw */
static void compile_cross(CalcGui *g) {
    const char *names[CALC_VARS + 1];
    double values[CALC_VARS + 1];
    int n = slots(g, names, values);
    for (int k = 0; k < CALC_PAIRS; k++) {
        int i, j;
        pair_of(k, &i, &j);
        g->cok[k] = 0;
        if (!g->ok[i] || !g->ok[j] || g->kind[i] != CALC_FUNC || g->kind[j] != CALC_FUNC) continue;
        char text[340];
        snprintf(text, sizeof text, "(%s) - (%s)", g->fx[i] + g->body[i], g->fx[j] + g->body[j]);
        g->cok[k] = sqm_parse(text, &scratch, NULL) == SQM_OK && sqm_compile(&scratch, names, n, &g->cprog[k]) == SQM_OK;
    }
}

/* parse part of row i (it starts at byte off of the row) and check that its
 * names other than var are defined; 0, or -1 with the row's error set */
static int find_list(const CalcGui *g, const char *name);

static int check_part2(CalcGui *g, int i, const char *text, int off, const char *var, const char *var2) {
    SqmError err;
    if (sqm_parse(text, &scratch, &err) != SQM_OK) {
        snprintf(g->err[i], sizeof g->err[i], "%s (at %d)", err.msg, err.pos + off + 1);
        return -1;
    }
    for (int k = 0; k < scratch.nnames; k++) {
        const char *nm = scratch.names[k];
        if (!strcmp(nm, var) || (var2 && !strcmp(nm, var2)) || !isnan(calc_gui_var(g, nm))) continue;
        if (!strcmp(nm, "x") && strcmp(var, "x")) snprintf(g->err[i], sizeof g->err[i], "curves use t, not x");
        else if (!strcmp(nm, "y") && !strcmp(var, "x"))
            snprintf(g->err[i], sizeof g->err[i], "with y, write an equation or inequality: x^2 + y^2 = 4");
        else if (find_list(g, nm) >= 0)
            snprintf(g->err[i], sizeof g->err[i], "%.15s is a list: try plot or linreg in the calculator", nm);
        else snprintf(g->err[i], sizeof g->err[i], "define %.15s first: %.15s = 1", nm, nm);
        return -1;
    }
    return 0;
}

static int check_part(CalcGui *g, int i, const char *text, int off, const char *var) {
    return check_part2(g, i, text, off, var, NULL);
}

/* compile text with x and y in slots 0 and 1 and the user variables after them */
static int compile_xy(CalcGui *g, int i, const char *text, SqmProgram *prog) {
    const char *names[CALC_VARS + 2];
    names[0] = "x";
    names[1] = "y";
    for (int k = 0; k < g->nvars; k++) names[k + 2] = g->vname[k];
    int rc = sqm_parse(text, &scratch, NULL);
    if (rc == SQM_OK) rc = sqm_compile(&scratch, names, g->nvars + 2, prog);
    if (rc != SQM_OK) snprintf(g->err[i], sizeof g->err[i], "%s", sqm_strerror(rc));
    return rc == SQM_OK ? 0 : -1;
}

/* compile text with var in slot 0 and the user variables after it */
static int compile_into(CalcGui *g, int i, const char *text, const char *var, SqmProgram *prog) {
    const char *names[CALC_VARS + 1];
    double values[CALC_VARS + 1];
    int n = slots(g, names, values);
    names[0] = var;
    int rc = sqm_parse(text, &scratch, NULL);
    if (rc == SQM_OK) rc = sqm_compile(&scratch, names, n, prog);
    if (rc != SQM_OK) snprintf(g->err[i], sizeof g->err[i], "%s", sqm_strerror(rc));
    return rc == SQM_OK ? 0 : -1;
}

/* "(X, Y)" as a whole: the byte offset of its top-level comma (and of the
 * closing parenthesis in *close), or 0 */
static int pair_comma(const char *p, int *close) {
    if (*p != '(') return 0;
    int depth = 0, comma = 0;
    size_t k = 0;
    for (; p[k]; k++) {
        if (p[k] == '(') depth++;
        else if (p[k] == ')' && --depth == 0) break;
        else if (p[k] == ',' && depth == 1 && !comma) comma = (int)k;
        else if (p[k] == ',' && depth == 1) return 0; /* three parts */
    }
    if (!p[k]) return 0;
    *close = (int)k;
    for (k++; p[k] == ' '; k++) {}
    return p[k] == 0 ? comma : 0;
}

/* a row with = < <= > >=: "y = f(x)" is a function; anything else with x and
 * y is an equation (f = 0, drawn as a curve) or an inequality (f < 0, shaded) */
static void compile_relation(CalcGui *g, int i, const char *row) {
    int at = -1, len = 0, n = 0;
    char op = 0;
    for (int k = 0; row[k]; k++) {
        if (row[k] != '<' && row[k] != '>' && row[k] != '=') continue;
        int l = (row[k] != '=' && row[k + 1] == '=') ? 2 : 1;
        if (n++ == 0) {
            at = k;
            op = row[k];
            len = l;
        }
        k += l - 1;
    }
    if (n > 1) {
        snprintf(g->err[i], sizeof g->err[i], "one =, < or > in a row");
        return;
    }
    char lhs[160], rhs[160];
    snprintf(lhs, sizeof lhs, "%.*s", at, row);
    snprintf(rhs, sizeof rhs, "%s", row + at + len);
    if (strspn(lhs, " ") == strlen(lhs) || strspn(rhs, " ") == strlen(rhs)) {
        snprintf(g->err[i], sizeof g->err[i], "something is missing beside %c", op);
        return;
    }
    const char *l = lhs + strspn(lhs, " ");
    size_t ll = strlen(l);
    while (ll && l[ll - 1] == ' ') ll--;
    int rhs_has_y = sqm_parse(rhs, &scratch, NULL) == SQM_OK && sqm_uses(&scratch, "y");
    if (op == '=' && ll == 1 && l[0] == 'y' && !rhs_has_y) { /* y = f(x) */
        g->body[i] = at + len;
        if (check_part(g, i, rhs, at + len, "x") == 0) g->ok[i] = !compile_into(g, i, rhs, "x", &g->prog[i]);
        return;
    }
    if (check_part2(g, i, lhs, 0, "x", "y") || check_part2(g, i, rhs, at + len, "x", "y")) return;
    char f[340];
    if (op == '>') snprintf(f, sizeof f, "(%s) - (%s)", rhs, lhs); /* a > b is b - a < 0 */
    else snprintf(f, sizeof f, "(%s) - (%s)", lhs, rhs);
    g->kind[i] = op == '=' ? CALC_IMPLICIT : CALC_INEQ;
    g->strict[i] = op != '=' && len == 1;
    g->ok[i] = !compile_xy(g, i, f, &g->prog[i]);
}

static void compile(CalcGui *g, int i) {
    memcpy(g->seen[i], g->fx[i], sizeof g->seen[i]);
    g->ok[i] = 0;
    g->kind[i] = CALC_FUNC;
    g->body[i] = 0;
    g->strict[i] = 0;
    g->err[i][0] = 0;
    g->nroots[i] = 0;
    const char *row = g->fx[i], *p = row;
    while (*p == ' ') p++;
    const char *q = p + 1;
    while (*q == ' ') q++;
    int close = 0, comma = pair_comma(p, &close);
    char a[200], b[200];
    if (*p == 'r' && *q == '=') {
        /* polar: r = f(t), drawn as (f cos t, f sin t) */
        g->kind[i] = CALC_POLAR;
        const char *e = q + 1;
        if (check_part(g, i, e, (int)(e - row), "t") == 0) {
            snprintf(a, sizeof a, "(%s)*cos(t)", e);
            snprintf(b, sizeof b, "(%s)*sin(t)", e);
            g->ok[i] = !compile_into(g, i, a, "t", &g->prog[i]) && !compile_into(g, i, b, "t", &g->prog2[i]);
        }
    } else if (comma) {
        /* parametric: (x(t), y(t)) */
        g->kind[i] = CALC_PARAM;
        snprintf(a, sizeof a, "%.*s", comma - 1, p + 1);
        snprintf(b, sizeof b, "%.*s", close - comma - 1, p + comma + 1);
        int off = (int)(p - row);
        if (check_part(g, i, a, off + 1, "t") == 0 && check_part(g, i, b, off + comma + 1, "t") == 0)
            g->ok[i] = !compile_into(g, i, a, "t", &g->prog[i]) && !compile_into(g, i, b, "t", &g->prog2[i]);
    } else if (strpbrk(p, "=<>")) {
        compile_relation(g, i, row);
    } else if (*p) {
        if (check_part(g, i, row, 0, "x") == 0) g->ok[i] = !compile_into(g, i, row, "x", &g->prog[i]);
    }
    if (i == 0) compile_deriv(g);
    compile_cross(g);
    if (g->area_on && g->area_fn == i) g->area_on = 0; /* the area was of the old text */
    if (g->trace_fn == i && !g->ok[i]) g->trace_fn = -1;
}

void calc_gui_set(CalcGui *g, int i, const char *text) {
    if (i < 0 || i >= CALC_N) return;
    if (text != g->fx[i]) snprintf(g->fx[i], sizeof g->fx[i], "%s", text ? text : "");
    compile(g, i);
}

static void hist_add(CalcGui *g, const char *line) {
    size_t n = strlen(line), have = strlen(g->hist);
    while (have && have + n + 2 > sizeof g->hist) { /* drop the oldest lines */
        char *nl = strchr(g->hist, '\n');
        if (!nl) {
            g->hist[0] = 0;
            have = 0;
            break;
        }
        memmove(g->hist, nl + 1, strlen(nl + 1) + 1);
        have = strlen(g->hist);
    }
    if (n + 2 > sizeof g->hist) n = sizeof g->hist - 2;
    if (have) g->hist[have++] = '\n';
    memcpy(g->hist + have, line, n);
    g->hist[have + n] = 0;
}

static int is_reserved(const char *name) {
    if (!strcmp(name, "x") || !strcmp(name, "y") || !strcmp(name, "pi") || !strcmp(name, "e")) return 1;
    for (int f = 0; f < SQM_FN_COUNT; f++)
        if (!strcmp(name, sqm_fn_name(f))) return 1;
    return 0;
}

static int set_var(CalcGui *g, const char *name, double v) {
    for (int i = 0; i < g->nvars; i++)
        if (!strcmp(g->vname[i], name)) {
            g->vval[i] = v;
            return 0;
        }
    if (g->nvars >= CALC_VARS) return -1;
    snprintf(g->vname[g->nvars], SQM_NAME_LEN, "%s", name);
    g->vval[g->nvars++] = v;
    return 0;
}

/* a slider range around v: -10..10, or wider for big values */
static void set_range(CalcGui *g, const char *name) {
    for (int i = 0; i < g->nvars; i++)
        if (!strcmp(g->vname[i], name)) {
            double v = g->vval[i], span = fabs(v) * 2 > 10 ? fabs(v) * 2 : 10;
            if (!isfinite(span)) span = 10;
            if (!(v >= g->vlo[i] && v <= g->vhi[i]) || g->vlo[i] == g->vhi[i]) {
                g->vlo[i] = -span;
                g->vhi[i] = span;
            }
        }
}

/* "word rest": rest after the word and spaces, or NULL */
static const char *command(const char *line, const char *word) {
    while (*line == ' ') line++;
    size_t n = strlen(word);
    if (strncmp(line, word, n) || line[n] != ' ') return NULL;
    line += n;
    while (*line == ' ') line++;
    return line;
}

static void hist_error(CalcGui *g, const char *line, const char *msg) {
    char out[400];
    snprintf(out, sizeof out, "%.200s: %s", line, msg);
    hist_add(g, out);
}

/* parse text (an equation when eq), or put the error in the history */
static int parse_or_say(CalcGui *g, const char *line, const char *text, int eq, SqmExpr *e) {
    SqmError err;
    if ((eq ? sqm_parse_eq(text, e, &err) : sqm_parse(text, e, &err)) == SQM_OK) return 0;
    hist_error(g, line, err.msg);
    return -1;
}

/* the value of a constant expression with the user variables, or NAN */
static double value_of(const CalcGui *g, const char *text) {
    const char *names[CALC_VARS];
    for (int i = 0; i < g->nvars; i++) names[i] = g->vname[i];
    double v;
    if (sqm_parse(text, &scratch2, NULL) != SQM_OK || sqm_eval(&scratch2, names, g->vval, g->nvars, &v) != SQM_OK)
        return NAN;
    return v;
}

static void load_values(const CalcGui *g, double *values);

/* integrate EXPR from A to B: EXPR is y1..y4 or any expression of x */
static int integrate_line(CalcGui *g, const char *line, const char *rest) {
    char expr[200], a_text[64], b_text[64], out[400];
    const char *from = strstr(rest, " from "), *to = from ? strstr(from + 6, " to ") : NULL;
    if (!from || !to || from == rest || (size_t)(from - rest) >= sizeof expr || (size_t)(to - from - 6) >= sizeof a_text ||
        strlen(to + 4) >= sizeof b_text) {
        hist_error(g, line, "write integrate EXPR from A to B");
        return -1;
    }
    memcpy(expr, rest, (size_t)(from - rest));
    expr[from - rest] = 0;
    memcpy(a_text, from + 6, (size_t)(to - from - 6));
    a_text[to - from - 6] = 0;
    snprintf(b_text, sizeof b_text, "%s", to + 4);
    double a = value_of(g, a_text), b = value_of(g, b_text);
    if (!isfinite(a) || !isfinite(b)) {
        hist_error(g, line, "the bounds need values");
        return -1;
    }
    int fn = -1;
    const char *text = expr;
    if (expr[0] == 'y' && expr[1] >= '1' && expr[1] <= '0' + CALC_N && expr[2] == 0) {
        fn = expr[1] - '1';
        if (!g->ok[fn]) {
            hist_error(g, line, g->fx[fn][0] ? "that function has an error" : "that function is empty");
            return -1;
        }
        if (g->kind[fn] != CALC_FUNC) {
            hist_error(g, line, "that row is a curve, not a function of x");
            return -1;
        }
        text = g->fx[fn] + g->body[fn];
    }
    if (parse_or_say(g, line, text, 0, &scratch)) return -1;
    const char *names[CALC_VARS + 1];
    double values[CALC_VARS + 1];
    int n = slots(g, names, values);
    int rc = sqm_compile(&scratch, names, n, &g->area_prog);
    if (rc != SQM_OK) {
        hist_error(g, line, rc == SQM_ERR_UNKNOWN ? "a name has no value" : sqm_strerror(rc));
        return -1;
    }
    load_values(g, values);
    int ok;
    double v = sqm_integrate(&g->area_prog, values, 0, a, b, 1e-10, &ok);
    char num[64];
    sqm_format(v, 12, num, sizeof num);
    snprintf(out, sizeof out, "%.200s = %s%s", line, num, ok ? "" : "  (may not have converged)");
    hist_add(g, out);
    if (isfinite(v)) set_var(g, "ans", v);
    g->area_on = 1;
    g->area_fn = fn;
    g->area_a = a;
    g->area_b = b;
    g->area_value = v;
    return 0;
}


/* ---- lists ---- */

static int find_list(const CalcGui *g, const char *name) {
    for (int k = 0; k < g->nlists; k++)
        if (!strcmp(g->lname[k], name)) return k;
    return -1;
}

/* "{1, 2, 3}" shortened past 8 items */
static void list_text(const CalcGui *g, int k, char *out, size_t cap) {
    size_t at = (size_t)snprintf(out, cap, "%s = {", g->lname[k]);
    for (int i = 0; i < g->llen[k] && i < 8 && at < cap; i++) {
        char t[32];
        sqm_format(g->lval[k][i], 6, t, sizeof t);
        at += (size_t)snprintf(out + at, cap - at, "%s%s", i ? ", " : "", t);
    }
    if (at < cap && g->llen[k] > 8) at += (size_t)snprintf(out + at, cap - at, ", ...} (%d items)", g->llen[k]);
    else if (at < cap) snprintf(out + at, cap - at, "}");
}

/* NAME = {a, b, c} (or [a, b, c]): each item any expression with a value */
static int list_assign(CalcGui *g, const char *line, const char *name, const char *rest) {
    while (*rest == ' ') rest++;
    char close = *rest == '{' ? '}' : ']';
    const char *end = strrchr(rest, close);
    if (!end || end[1 + strspn(end + 1, " ")]) {
        hist_error(g, line, close == '}' ? "a list ends with }" : "a list ends with ]");
        return -1;
    }
    if (!isnan(calc_gui_var(g, name))) {
        hist_error(g, line, "that name is a number already");
        return -1;
    }
    double vals[CALC_LIST_MAX];
    int n = 0;
    const char *p = rest + 1;
    if (strspn(p, " ") != (size_t)(end - p)) {
        for (;;) {
            int depth = 0;
            const char *q = p;
            for (; q < end && !(depth == 0 && *q == ','); q++) depth += (*q == '(') - (*q == ')');
            char item[96];
            if ((size_t)(q - p) >= sizeof item || n >= CALC_LIST_MAX) {
                hist_error(g, line, n >= CALC_LIST_MAX ? "a list holds 100 values" : "an item is too long");
                return -1;
            }
            memcpy(item, p, (size_t)(q - p));
            item[q - p] = 0;
            vals[n] = value_of(g, item);
            if (!isfinite(vals[n])) {
                char msg[64];
                snprintf(msg, sizeof msg, "item %d has no value", n + 1);
                hist_error(g, line, msg);
                return -1;
            }
            n++;
            if (q >= end) break;
            p = q + 1;
        }
    }
    if (n == 0) {
        hist_error(g, line, "a list needs a value");
        return -1;
    }
    int k = find_list(g, name);
    if (k < 0) {
        if (g->nlists >= CALC_LISTS) {
            hist_error(g, line, "no room for more lists");
            return -1;
        }
        k = g->nlists++;
        snprintf(g->lname[k], SQM_NAME_LEN, "%s", name);
    }
    memcpy(g->lval[k], vals, (size_t)n * sizeof *vals);
    g->llen[k] = n;
    char out[400];
    list_text(g, k, out, sizeof out);
    hist_add(g, out);
    return 0;
}

/* two list names after a command: "xs ys" */
static int two_lists(CalcGui *g, const char *line, const char *rest, int *a, int *b) {
    char na[SQM_NAME_LEN], nb[SQM_NAME_LEN], extra[2];
    if (sscanf(rest, "%15s %15s %1s", na, nb, extra) != 2) {
        hist_error(g, line, "name two lists: linreg xs ys");
        return -1;
    }
    *a = find_list(g, na);
    *b = find_list(g, nb);
    if (*a < 0 || *b < 0) {
        char msg[64];
        snprintf(msg, sizeof msg, "%s is not a list", *a < 0 ? na : nb);
        hist_error(g, line, msg);
        return -1;
    }
    if (g->llen[*a] != g->llen[*b]) {
        hist_error(g, line, "the lists are not the same length");
        return -1;
    }
    return 0;
}

/* stats L, plot X Y, linreg / quadreg / expreg X Y; -2 when not one of them */
static int list_line(CalcGui *g, const char *line) {
    static const char *const FITS[3] = {"linreg", "quadreg", "expreg"};
    char out[400];
    const char *rest;
    if ((rest = command(line, "stats"))) {
        char name[SQM_NAME_LEN], extra[2];
        int k = sscanf(rest, "%15s %1s", name, extra) == 1 ? find_list(g, name) : -1;
        CalcStats st;
        if (k < 0 || calc_stats(g->lval[k], g->llen[k], &st)) {
            hist_error(g, line, "name one list: stats xs");
            return -1;
        }
        char a[32], b[32], c[32], d[32], e[32], f[32];
        sqm_format(st.mean, 6, a, sizeof a);
        sqm_format(st.sd, 6, b, sizeof b);
        sqm_format(st.min, 6, c, sizeof c);
        sqm_format(st.median, 6, d, sizeof d);
        sqm_format(st.max, 6, e, sizeof e);
        sqm_format(st.sum, 6, f, sizeof f);
        hist_add(g, line);
        snprintf(out, sizeof out, "  n = %d  mean = %s  sd = %s", st.n, a, b);
        hist_add(g, out);
        snprintf(out, sizeof out, "  min = %s  median = %s  max = %s  sum = %s", c, d, e, f);
        hist_add(g, out);
        set_var(g, "ans", st.mean);
        return 0;
    }
    if ((rest = command(line, "plot"))) {
        int a, b;
        if (two_lists(g, line, rest, &a, &b)) return -1;
        g->fit_on = 1;
        g->fit_lx = a;
        g->fit_ly = b;
        g->fit_view = 1;
        snprintf(out, sizeof out, "%s: %d points", line, g->llen[a]);
        hist_add(g, out);
        return 0;
    }
    for (int kind = 0; kind < 3; kind++) {
        if (!(rest = command(line, FITS[kind]))) continue;
        int a, b;
        if (two_lists(g, line, rest, &a, &b)) return -1;
        const char *why;
        CalcFit fit;
        if (calc_fit(kind, g->lval[a], g->lval[b], g->llen[a], &fit, &why)) {
            hist_error(g, line, why);
            return -1;
        }
        g->fit = fit;
        g->fit_on = 2;
        g->fit_lx = a;
        g->fit_ly = b;
        g->fit_view = 1;
        char eq[160], r2[32], r[32];
        calc_fit_text(&fit, eq, sizeof eq);
        sqm_format(fit.r2, 6, r2, sizeof r2);
        hist_add(g, line);
        snprintf(out, sizeof out, "  %s", eq);
        hist_add(g, out);
        if (kind == CALC_FIT_LINEAR) {
            sqm_format(fit.r, 6, r, sizeof r);
            snprintf(out, sizeof out, "  r^2 = %s  r = %s", r2, r);
        } else {
            snprintf(out, sizeof out, "  r^2 = %s", r2);
        }
        hist_add(g, out);
        set_var(g, "ans", fit.r2);
        return 0;
    }
    return -2;
}

/* CAS lines: expand EXPR, collect VAR EXPR, d/dx EXPR, solve EQUATION [for VAR].
 * Returns 0 or -1 when it was one, -2 when the line is not a command. */
static int cas_line(CalcGui *g, const char *line) {
    char out[400], text[200];
    const char *rest;
    int rc;
    if ((rest = command(line, "expand")) || (rest = command(line, "d/dx"))) {
        int diff = !command(line, "expand");
        if (parse_or_say(g, line, rest, 0, &scratch)) return -1;
        rc = diff ? sqm_diff(&scratch, "x", &scratch2) : sqm_expand(&scratch, &scratch2);
    } else if ((rest = command(line, "collect"))) {
        char var[SQM_NAME_LEN];
        size_t n = strcspn(rest, " ");
        if (n == 0 || n >= sizeof var || !rest[n]) {
            hist_error(g, line, "write collect VAR EXPR");
            return -1;
        }
        memcpy(var, rest, n);
        var[n] = 0;
        if (parse_or_say(g, line, rest + n, 0, &scratch)) return -1;
        rc = sqm_collect(&scratch, var, &scratch2);
    } else if ((rest = command(line, "solve"))) {
        /* "... for v" picks the variable, x otherwise */
        char var[SQM_NAME_LEN] = "x";
        snprintf(text, sizeof text, "%s", rest);
        char *f = strstr(text, " for ");
        if (f) {
            const char *v = f + 5;
            while (*v == ' ') v++;
            size_t n = strcspn(v, " ");
            if (n == 0 || n >= sizeof var || v[n + strspn(v + n, " ")] || !isalpha((unsigned char)v[0])) {
                hist_error(g, line, "write solve EQUATION for VAR");
                return -1;
            }
            memcpy(var, v, n);
            var[n] = 0;
            *f = 0;
        }
        if (parse_or_say(g, line, text, 1, &scratch)) return -1;
        SqmSolution sol[8];
        int n;
        rc = sqm_solve_for(&scratch, var, sol, 8, &n);
        hist_add(g, line);
        if (n < 0) {
            snprintf(out, sizeof out, "  every %s", var);
            hist_add(g, out);
        } else if (n == 0 && rc == SQM_OK) {
            hist_add(g, "  no real solution");
        }
        const char *names[CALC_VARS];
        for (int i = 0; i < g->nvars; i++) names[i] = g->vname[i];
        for (int k = 0; k < n; k++) {
            char v[32];
            /* a symbolic answer gets a value too when its names are defined */
            if (isnan(sol[k].value) && sqm_parse(sol[k].text, &scratch2, NULL) == SQM_OK &&
                sqm_eval(&scratch2, names, g->vval, g->nvars, &sol[k].value) != SQM_OK)
                sol[k].value = NAN;
            sqm_format(sol[k].value, 6, v, sizeof v);
            if (isnan(sol[k].value) || !strcmp(v, sol[k].text)) snprintf(out, sizeof out, "  %s = %.128s", var, sol[k].text);
            else snprintf(out, sizeof out, "  %s = %.128s  (%s)", var, sol[k].text, v);
            hist_add(g, out);
        }
        if (rc != SQM_OK) {
            snprintf(out, sizeof out, "  %s%s", n > 0 ? "and more: " : "", sqm_strerror(rc));
            hist_add(g, out);
        }
        if (n > 0 && isfinite(sol[0].value)) set_var(g, "ans", sol[0].value);
        return rc == SQM_OK ? 0 : -1;
    } else if ((rest = command(line, "integrate"))) {
        return integrate_line(g, line, rest);
    } else {
        return -2;
    }
    if (rc != SQM_OK) {
        hist_error(g, line, sqm_strerror(rc));
        return -1;
    }
    sqm_print(&scratch2, text, sizeof text);
    snprintf(out, sizeof out, "%.200s = %.190s", line, text);
    hist_add(g, out);
    return 0;
}

int calc_gui_eval(CalcGui *g, const char *line) {
    int cas = cas_line(g, line);
    if (cas == -2) cas = list_line(g, line);
    if (cas != -2) return cas;
    {
        /* a list's name alone shows it */
        char name[SQM_NAME_LEN], extra[2];
        int k = sscanf(line, "%15s %1s", name, extra) == 1 ? find_list(g, name) : -1;
        if (k >= 0) {
            char out[400];
            list_text(g, k, out, sizeof out);
            hist_add(g, out);
            return 0;
        }
    }
    char out[400], num[64], name[SQM_NAME_LEN] = "";
    const char *expr = line;
    /* name = expression */
    const char *eq = strchr(line, '=');
    if (eq) {
        const char *a = line, *b = eq;
        while (*a == ' ') a++;
        while (b > a && b[-1] == ' ') b--;
        size_t len = (size_t)(b - a);
        int good = len > 0 && len < SQM_NAME_LEN && ((*a >= 'a' && *a <= 'z') || (*a >= 'A' && *a <= 'Z') || *a == '_');
        for (size_t k = 0; good && k < len; k++)
            good = (a[k] >= 'a' && a[k] <= 'z') || (a[k] >= 'A' && a[k] <= 'Z') || a[k] == '_' ||
                   (a[k] >= '0' && a[k] <= '9');
        if (!good) {
            snprintf(out, sizeof out, "%s: the left of = must be a name", line);
            hist_add(g, out);
            return -1;
        }
        memcpy(name, a, len);
        name[len] = 0;
        if (is_reserved(name) || !strcmp(name, "ans")) {
            snprintf(out, sizeof out, "%s: %s can't be assigned", line, name);
            hist_add(g, out);
            return -1;
        }
        expr = eq + 1;
        const char *b2 = expr + strspn(expr, " ");
        if (*b2 == '{' || *b2 == '[') return list_assign(g, line, name, b2);
        if (find_list(g, name) >= 0) {
            snprintf(out, sizeof out, "%s: %s is a list", line, name);
            hist_add(g, out);
            return -1;
        }
    }
    SqmError err;
    if (sqm_parse(expr, &scratch, &err) != SQM_OK) {
        snprintf(out, sizeof out, "%s: %s", line, err.msg);
        hist_add(g, out);
        return -1;
    }
    const char *names[CALC_VARS];
    for (int i = 0; i < g->nvars; i++) names[i] = g->vname[i];
    double v;
    if (sqm_eval(&scratch, names, g->vval, g->nvars, &v) != SQM_OK) {
        const char *missing = "?";
        for (int k = 0; k < scratch.nnames; k++)
            if (isnan(calc_gui_var(g, scratch.names[k]))) missing = scratch.names[k];
        snprintf(out, sizeof out, "%s: %s is not defined", line, missing);
        hist_add(g, out);
        return -1;
    }
    sqm_format(v, 12, num, sizeof num);
    if (name[0]) {
        if (set_var(g, name, v)) {
            snprintf(out, sizeof out, "%s: no room for more variables", line);
            hist_add(g, out);
            return -1;
        }
        set_range(g, name);
        snprintf(out, sizeof out, "%s = %s", name, num);
        for (int i = 0; i < CALC_N; i++) compile(g, i); /* the graphs may use it */
    } else {
        set_var(g, "ans", v);
        snprintf(out, sizeof out, "%s = %s", line, num);
    }
    hist_add(g, out);
    return 0;
}

/* the current values for the programs: x, then the variables */
static void load_values(const CalcGui *g, double *values) {
    values[0] = 0;
    for (int i = 0; i < g->nvars; i++) values[i + 1] = g->vval[i];
}

static double fit_y(const void *ctx, double x) { return calc_fit_eval((const CalcFit *)ctx, x); }

static void graph(CalcGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    const sqg_input *in = &ui->in;
    if (!g->view_ready || g->graph_at.w != r.w || g->graph_at.h != r.h) {
        if (!g->view_ready) calc_view_default(&g->view, r.w, r.h);
        else calc_view_square(&g->view, r.w, r.h);
        g->view_ready = 1;
    }
    g->graph_at = r;
    sqg_rect inner = sqg_rect_inset(r, 2);
    if (g->fit_view && g->fit_on) { /* the points and a margin of a tenth */
        const double *xs = g->lval[g->fit_lx], *ys = g->lval[g->fit_ly];
        double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
        for (int k = 1; k < g->llen[g->fit_lx]; k++) {
            x0 = fmin(x0, xs[k]);
            x1 = fmax(x1, xs[k]);
            y0 = fmin(y0, ys[k]);
            y1 = fmax(y1, ys[k]);
        }
        double mx = fmax((x1 - x0) / 10, 1e-6 + fabs(x0) * 1e-9), my = fmax((y1 - y0) / 10, 1e-6 + fabs(y0) * 1e-9);
        if (x1 - x0 < 1e-9) mx = fmax(1, fabs(x0) / 10);
        if (y1 - y0 < 1e-9) my = fmax(1, fabs(y0) / 10);
        g->view.xmin = x0 - mx;
        g->view.xmax = x1 + mx;
        g->view.ymin = y0 - my;
        g->view.ymax = y1 + my;
    }
    g->fit_view = 0;

    /* pointer: wheel zooms about it, a drag pans */
    int inside = sqg_rect_contains(inner, in->mx, in->my);
    g->mouse_in = inside;
    if (inside) {
        g->mouse_x = calc_x_at(&g->view, inner, in->mx + 0.5);
        g->mouse_y = calc_y_at(&g->view, inner, in->my + 0.5);
        if (in->wheel) calc_view_zoom(&g->view, pow(1.25, in->wheel), g->mouse_x, g->mouse_y);
        if (in->pressed && !ui->hot) {
            g->dragging = 1;
            g->last_mx = g->press_mx = in->mx;
            g->last_my = g->press_my = in->my;
            ui->focus = 0;
        }
    }
    if (g->dragging && in->released && abs(in->mx - g->press_mx) + abs(in->my - g->press_my) < 4) {
        /* a click, not a drag: trace the curve nearest the pointer, or stop */
        double values[CALC_VARS + 1];
        load_values(g, values);
        values[0] = calc_x_at(&g->view, inner, in->mx + 0.5);
        int best = -1;
        double best_d = 10;
        for (int i = 0; i < CALC_N; i++) {
            if (!g->ok[i] || g->kind[i] != CALC_FUNC) continue;
            double d = fabs(calc_px_y(&g->view, inner, sqm_run(&g->prog[i], values)) - (in->my + 0.5));
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        g->trace_fn = best;
        g->trace_x = values[0];
    }
    if (g->trace_fn >= 0 && !ui->focus) { /* Left and Right step one pixel */
        double step = (g->view.xmax - g->view.xmin) / inner.w;
        if (in->key == SQG_KEY_LEFT) g->trace_x -= step;
        if (in->key == SQG_KEY_RIGHT) g->trace_x += step;
    }
    if (g->dragging) {
        if (in->down) {
            calc_view_pan(&g->view, in->mx - g->last_mx, in->my - g->last_my, inner.w, inner.h);
            g->last_mx = in->mx;
            g->last_my = in->my;
        } else {
            g->dragging = 0;
        }
    }

    sqg_bevel(cv, r, 1, 0, ui->theme);
    calc_draw_axes(cv, inner, &g->view);
    double values[CALC_VARS + 1];
    load_values(g, values);
    if (g->area_on) {
        uint32_t col = g->area_fn >= 0 ? COLORS[g->area_fn] : 0xFF808080u;
        calc_shade(cv, inner, &g->view, &g->area_prog, values, 0, g->area_a, g->area_b, (col & 0xFFFFFFu) | 0x50000000u);
    }
    if (g->show_deriv && g->dok) calc_plot(cv, inner, &g->view, &g->dprog, values, 0, COL_DERIV);
    double t0 = calc_gui_var(g, "tmin"), t1 = calc_gui_var(g, "tmax");
    if (isnan(t0)) t0 = 0;
    if (isnan(t1)) t1 = 2 * 3.14159265358979323846;
    for (int i = 0; i < CALC_N; i++) {
        if (!g->ok[i]) continue;
        g->nroots[i] = 0;
        if (g->kind[i] == CALC_IMPLICIT || g->kind[i] == CALC_INEQ) {
            double xy[CALC_VARS + 2] = {0};
            for (int k = 0; k < g->nvars; k++) xy[k + 2] = g->vval[k];
            if (g->kind[i] == CALC_INEQ)
                calc_shade_region(cv, inner, &g->view, &g->prog[i], xy, 0, 1, 3, (COLORS[i] & 0xFFFFFFu) | 0x40000000u);
            calc_plot_implicit(cv, inner, &g->view, &g->prog[i], xy, 0, 1, 3, g->strict[i], COLORS[i]);
            continue;
        }
        if (g->kind[i] != CALC_FUNC) {
            calc_plot_curve(cv, inner, &g->view, &g->prog[i], &g->prog2[i], values, 0, t0, t1, 1000, COLORS[i]);
            values[0] = 0;
            continue;
        }
        calc_plot(cv, inner, &g->view, &g->prog[i], values, 0, COLORS[i]);
        if (g->show_roots) {
            int n = sqm_roots(&g->prog[i], values, 0, g->view.xmin, g->view.xmax, inner.w, g->roots[i], CALC_ROOTS);
            g->nroots[i] = n < CALC_ROOTS ? n : CALC_ROOTS;
            calc_mark_roots(cv, inner, &g->view, g->roots[i], g->nroots[i], COLORS[i]);
        }
    }
    /* list points, and the fitted curve */
    if (g->fit_on && g->fit_lx < g->nlists && g->fit_ly < g->nlists && g->llen[g->fit_lx] == g->llen[g->fit_ly]) {
        if (g->fit_on == 2) calc_plot_fn(cv, inner, &g->view, fit_y, &g->fit, COL_FIT);
        calc_mark_points(cv, inner, &g->view, g->lval[g->fit_lx], g->lval[g->fit_ly], g->llen[g->fit_lx], COL_POINTS);
    }
    /* crossings of each pair, in view */
    g->ncross = 0;
    for (int k = 0; k < CALC_PAIRS && g->show_cross; k++) {
        if (!g->cok[k]) continue;
        int i, j;
        pair_of(k, &i, &j);
        double xs[CALC_CROSS];
        int n = sqm_roots(&g->cprog[k], values, 0, g->view.xmin, g->view.xmax, inner.w, xs, CALC_CROSS);
        for (int m = 0; m < n && m < CALC_CROSS && g->ncross < CALC_CROSS; m++) {
            values[0] = xs[m];
            double y = sqm_run(&g->prog[i], values);
            if (!isfinite(y)) continue;
            g->cross_x[g->ncross] = xs[m];
            g->cross_y[g->ncross] = y;
            g->cross_pair[g->ncross++] = k;
        }
    }
    calc_mark_points(cv, inner, &g->view, g->cross_x, g->cross_y, g->ncross, 0xFF303030u);
    /* the traced point and its value */
    if (g->trace_fn >= 0 && g->ok[g->trace_fn]) {
        values[0] = g->trace_x;
        double y = sqm_run(&g->prog[g->trace_fn], values);
        char a[32], b[32], t[96];
        sqm_format(g->trace_x, 6, a, sizeof a);
        sqm_format(y, 6, b, sizeof b);
        snprintf(t, sizeof t, "y%d(%s) = %s", g->trace_fn + 1, a, b);
        int tx = inner.x + 6, ty = inner.y + 4;
        if (isfinite(y)) {
            calc_mark_points(cv, inner, &g->view, &g->trace_x, &y, 1, COLORS[g->trace_fn]);
            tx = (int)calc_px_x(&g->view, inner, g->trace_x) + 8;
            ty = (int)calc_px_y(&g->view, inner, y) - 14;
        }
        int tw = sqg_text_width(t, 1);
        if (tx + tw > inner.x + inner.w - 2) tx = inner.x + inner.w - 2 - tw;
        if (tx < inner.x + 2) tx = inner.x + 2;
        if (ty < inner.y + 2) ty = inner.y + 2;
        if (ty > inner.y + inner.h - 12) ty = inner.y + inner.h - 12;
        sqg_fill(cv, sqg_rect_make(tx - 2, ty - 2, tw + 4, 12), 0xFFFFFFFFu);
        sqg_text(cv, tx, ty, t, COLORS[g->trace_fn], 1);
    }
}

/* x and y1..y4 in rows from Start by Step; the wheel scrolls */
static void table(CalcGui *g, sqg_ui *ui, sqg_rect r) {
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui);
    sqg_row row = sqg_row_begin(sqg_rect_make(r.x, r.y, r.w, lh), 4);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width("Start", 1) + 4), "Start", SQG_ALIGN_LEFT);
    sqg_textfield(ui, sqg_id_str("tstart"), sqg_row_take(&row, 80), g->tstart, sizeof g->tstart);
    sqg_label(ui, sqg_row_take(&row, sqg_text_width("Step", 1) + 4), "Step", SQG_ALIGN_LEFT);
    sqg_textfield(ui, sqg_id_str("tstep"), sqg_row_take(&row, 80), g->tstep, sizeof g->tstep);
    double start = value_of(g, g->tstart), step = value_of(g, g->tstep);
    sqg_rect body = sqg_rect_make(r.x, r.y + lh + 4, r.w, r.h - lh - 4);
    if (body.h < 30) return;
    sqg_bevel(cv, body, 1, 0, ui->theme);
    sqg_rect in = sqg_rect_inset(body, 2);
    sqg_fill(cv, in, 0xFFFFFFFFu);
    if (!isfinite(start) || !(step > 0) || !isfinite(step)) {
        sqg_text(cv, in.x + 6, in.y + 6, "Start needs a value and Step a value above 0", COL_ERR, 1);
        return;
    }
    if (sqg_rect_contains(in, ui->in.mx, ui->in.my)) g->table_top -= ui->in.wheel * 3;
    int cols[CALC_N + 1], ncol = 0;
    cols[ncol++] = -1;
    for (int i = 0; i < CALC_N; i++)
        if (g->ok[i] && g->kind[i] == CALC_FUNC) cols[ncol++] = i;
    int cw = in.w / ncol, rh = 14, rows = (in.h - rh - 4) / rh;
    sqg_rect old = sqg_clip(cv, in);
    for (int c = 0; c < ncol; c++) {
        char head[16];
        snprintf(head, sizeof head, cols[c] < 0 ? "x" : "y%d", cols[c] + 1);
        sqg_text(cv, in.x + c * cw + 6, in.y + 3, head, cols[c] < 0 ? 0xFF000000u : COLORS[cols[c]], 1);
        if (c) sqg_vline(cv, in.x + c * cw, in.y, in.y + in.h - 1, 0xFFE4E8EEu);
    }
    sqg_hline(cv, in.x, in.x + in.w - 1, in.y + rh, 0xFFC0C8D0u);
    double values[CALC_VARS + 1];
    load_values(g, values);
    for (int k = 0; k < rows; k++) {
        double x = start + step * (double)(g->table_top + k);
        int ty = in.y + rh + 3 + k * rh;
        if (k % 2) sqg_fill(cv, sqg_rect_make(in.x, ty - 2, in.w, rh), 0xFFF4F6F8u);
        values[0] = x;
        for (int c = 0; c < ncol; c++) {
            char t[32];
            sqm_format(cols[c] < 0 ? x : sqm_run(&g->prog[cols[c]], values), 6, t, sizeof t);
            sqg_text(cv, in.x + c * cw + 6, ty, t, 0xFF202020u, 1);
        }
    }
    cv->clip = old;
}

/* the roots and the crossings in view, or the hint */
static void make_status(CalcGui *g) {
    char s[200] = "";
    size_t k = 0;
    if (g->show_roots) {
        k += (size_t)snprintf(s + k, sizeof s - k, "Roots ");
        for (int i = 0; i < CALC_N && k + 40 < sizeof s; i++) {
            if (!g->ok[i] || g->kind[i] != CALC_FUNC) continue;
            k += (size_t)snprintf(s + k, sizeof s - k, "  y%d:", i + 1);
            if (g->nroots[i] == 0) k += (size_t)snprintf(s + k, sizeof s - k, " none in view");
            for (int r = 0; r < g->nroots[i] && r < 4 && k + 20 < sizeof s; r++) {
                char t[32];
                sqm_format(g->roots[i][r], 6, t, sizeof t);
                k += (size_t)snprintf(s + k, sizeof s - k, " %s", t);
            }
            if (g->nroots[i] > 4) k += (size_t)snprintf(s + k, sizeof s - k, " ...");
        }
    }
    if (g->show_cross && k + 40 < sizeof s) {
        k += (size_t)snprintf(s + k, sizeof s - k, "%sCrossings", k ? "    " : "");
        if (g->ncross == 0) k += (size_t)snprintf(s + k, sizeof s - k, " none in view");
        for (int m = 0; m < g->ncross && m < 4 && k + 50 < sizeof s; m++) {
            int i, j;
            pair_of(g->cross_pair[m], &i, &j);
            char a[32], b[32];
            sqm_format(g->cross_x[m], 5, a, sizeof a);
            sqm_format(g->cross_y[m], 5, b, sizeof b);
            k += (size_t)snprintf(s + k, sizeof s - k, "  y%d=y%d at (%s, %s)", i + 1, j + 1, a, b);
        }
        if (g->ncross > 4 && k + 5 < sizeof s) k += (size_t)snprintf(s + k, sizeof s - k, " ...");
    }
    if (g->fit_on == 2 && k + 60 < sizeof s) {
        char eq[120], r2[32];
        calc_fit_text(&g->fit, eq, sizeof eq);
        sqm_format(g->fit.r2, 5, r2, sizeof r2);
        k += (size_t)snprintf(s + k, sizeof s - k, "%sFit %.90s, r^2 = %s", k ? "    " : "", eq, r2);
    }
    if (k >= sizeof s) k = sizeof s - 1;
    snprintf(g->status, sizeof g->status, "%s", k ? s : HINT);
}

void calc_gui_frame(CalcGui *g, sqg_ui *ui) {
    sqg_canvas *cv = ui->cv;
    int lh = sqg_line_h(ui), pad = 6;
    sqg_fill(cv, sqg_rect_make(0, 0, cv->w, cv->h), ui->theme->face);
    for (int i = 0; i < CALC_N; i++)
        if (strcmp(g->fx[i], g->seen[i])) compile(g, i); /* typed into */

    int left_w = cv->w / 3;
    if (left_w < 240) left_w = 240;
    if (left_w > cv->w - 2 * pad - 40) left_w = cv->w - 2 * pad - 40;
    if (left_w < 40) left_w = 40;
    int status_y = cv->h - pad - lh;

    /* functions */
    int y = pad;
    sqg_label(ui, sqg_rect_make(pad, y, left_w, lh), "Functions, curves of t, equations, y < x", SQG_ALIGN_LEFT);
    y += lh;
    for (int i = 0; i < CALC_N; i++) {
        char id[16], tag[16];
        snprintf(id, sizeof id, "fx%d", i);
        snprintf(tag, sizeof tag, "y%d =", i + 1);
        sqg_row row = sqg_row_begin(sqg_rect_make(pad, y, left_w, lh), 4);
        sqg_rect sw = sqg_row_take(&row, 8);
        sqg_fill(cv, sqg_rect_make(sw.x, sw.y + lh / 2 - 4, 8, 8), COLORS[i]);
        sqg_label(ui, sqg_row_take(&row, sqg_text_width("y1 =", ui->scale) + 2), tag, SQG_ALIGN_LEFT);
        sqg_textfield(ui, sqg_id_str(id), sqg_row_take(&row, 0), g->fx[i], sizeof g->fx[i]);
        y += lh;
        if (g->err[i][0]) sqg_text(cv, pad + 14, y + 1, g->err[i], COL_ERR, 1);
        y += 12;
    }
    sqg_row trow = sqg_row_begin(sqg_rect_make(pad, y, left_w, lh), 6);
    sqg_toggle(ui, sqg_id_str("roots"), sqg_row_take(&trow, sqg_button_w(ui, "Roots")), "Roots", &g->show_roots);
    sqg_toggle(ui, sqg_id_str("cross"), sqg_row_take(&trow, sqg_button_w(ui, "Crossings")), "Crossings",
               &g->show_cross);
    sqg_toggle(ui, sqg_id_str("deriv"), sqg_row_take(&trow, sqg_button_w(ui, "d/dx y1")), "d/dx y1",
               &g->show_deriv);
    y += lh + 4;
    if (g->show_deriv && g->dtext[0]) {
        sqg_text(cv, pad, y, g->dtext, g->dok ? 0xFF3050A0u : COL_ERR, 1);
        y += 12;
    }

    /* a slider for each variable but ans */
    int ns = 0;
    for (int v = 0; v < g->nvars && ns < CALC_SLIDERS; v++) {
        if (!strcmp(g->vname[v], "ans")) continue;
        char id[24], val[32];
        snprintf(id, sizeof id, "slider_%s", g->vname[v]);
        sqg_row srow = sqg_row_begin(sqg_rect_make(pad, y, left_w, lh), 6);
        sqg_label(ui, sqg_row_take(&srow, sqg_text_width("tmax", 1) + 4), g->vname[v], SQG_ALIGN_LEFT);
        sqg_rect vr = sqg_row_take(&srow, 64);
        sqg_rect sr = sqg_row_take(&srow, 0);
        g->slider_at[ns++] = sr;
        sqg_slider(ui, sqg_id_str(id), sr, &g->vval[v], g->vlo[v], g->vhi[v]);
        sqm_format(g->vval[v], 4, val, sizeof val);
        sqg_label(ui, vr, val, SQG_ALIGN_LEFT);
        y += lh + 2;
    }
    for (int k = ns; k < CALC_SLIDERS; k++) g->slider_at[k] = sqg_rect_make(0, 0, 0, 0);

    /* calculator: history above the input line */
    y += 4;
    sqg_label(ui, sqg_rect_make(pad, y, left_w, lh), "Calculator (a = 2, solve x^2 = 2, xs = {1, 2})", SQG_ALIGN_LEFT);
    y += lh;
    int input_y = status_y - pad - lh;
    if (input_y - y > 20) sqg_textbox(ui, sqg_rect_make(pad, y, left_w, input_y - 4 - y), g->hist);
    sqg_row crow = sqg_row_begin(sqg_rect_make(pad, input_y, left_w, lh), 4);
    int enter = sqg_textfield(ui, sqg_id_str("line"), sqg_row_take(&crow, left_w - sqg_button_w(ui, "=") - 4),
                              g->line, sizeof g->line);
    if ((sqg_button(ui, sqg_id_str("eq"), sqg_row_take(&crow, 0), "=") || enter) && g->line[0]) {
        if (calc_gui_eval(g, g->line) == 0) g->line[0] = 0;
    }

    /* graph toolbar and graph */
    int gx = pad + left_w + pad, gw = cv->w - gx - pad;
    if (gw > 20) {
        sqg_row grow = sqg_row_begin(sqg_rect_make(gx, pad, gw, lh), 4);
        double cx = (g->view.xmin + g->view.xmax) / 2, cy = (g->view.ymin + g->view.ymax) / 2;
        if (sqg_button(ui, sqg_id_str("zout"), sqg_row_take(&grow, sqg_button_w(ui, "-")), "-"))
            calc_view_zoom(&g->view, 1 / 1.5, cx, cy);
        if (sqg_button(ui, sqg_id_str("zin"), sqg_row_take(&grow, sqg_button_w(ui, "+")), "+"))
            calc_view_zoom(&g->view, 1.5, cx, cy);
        if (sqg_button(ui, sqg_id_str("reset"), sqg_row_take(&grow, sqg_button_w(ui, "Reset")), "Reset"))
            g->view_ready = g->area_on = g->fit_on = 0;
        if (sqg_button(ui, sqg_id_str("square"), sqg_row_take(&grow, sqg_button_w(ui, "Square")), "Square"))
            calc_view_square(&g->view, g->graph_at.w, g->graph_at.h);
        sqg_toggle(ui, sqg_id_str("table"), sqg_row_take(&grow, sqg_button_w(ui, "Table")), "Table", &g->show_table);
        char pos[80] = "";
        if (g->mouse_in && !g->show_table) {
            char a[32], b[32];
            sqm_format(g->mouse_x, 5, a, sizeof a);
            sqm_format(g->mouse_y, 5, b, sizeof b);
            snprintf(pos, sizeof pos, "x = %s  y = %s", a, b);
        }
        sqg_label(ui, sqg_row_take(&grow, 0), pos, SQG_ALIGN_RIGHT);
        int top = pad + lh + 4;
        sqg_rect area = sqg_rect_make(gx, top, gw, status_y - pad - top);
        if (area.h > 20) {
            if (g->show_table) {
                g->mouse_in = 0;
                table(g, ui, area);
            } else {
                graph(g, ui, area);
            }
        }
    }

    make_status(g);
    sqg_label(ui, sqg_rect_make(pad, status_y, cv->w - 2 * pad, lh), g->status, SQG_ALIGN_LEFT);
}
