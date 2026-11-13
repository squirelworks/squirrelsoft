/* sqcalc: a command-line calculator over libs/sqmath (original code).
 *
 *   sqcalc "2x^2 - 1" x=3            evaluate (values may be expressions: x=pi/2)
 *   sqcalc --diff x "x^2 sin(x)"     derivative, printed (and its value with x=...)
 *   sqcalc --simplify "2x + 3x"
 *   sqcalc --expand "(x + 1)^3"
 *   sqcalc --collect x "a x^2 + b x^2 + x"
 *   sqcalc --solve-for x "x^2 - x = 1"   exact solutions: x = (1 - sqrt(5))/2 ...
 *   sqcalc --solve x -5 5 "x^2 - 2"  every sign-change root in [-5, 5]
 *   sqcalc --integrate x 0 pi "sin(x)"
 *   sqcalc --table x 0 1 0.25 "x^2"
 *   --digits N                       significant digits shown (default 12)
 *
 * Exit status: 0 done, 1 the expression has an error, 2 bad usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqmath.h"

#define MAXV 16

static int digits = 12;
static const char *vname[MAXV];
static double vval[MAXV];
static int nv;

static void usage(FILE *f) {
    fprintf(f,
            "usage: sqcalc [--digits N] EXPR [name=value ...]\n"
            "       sqcalc --diff VAR EXPR [name=value ...]\n"
            "       sqcalc --simplify EXPR\n"
            "       sqcalc --expand EXPR\n"
            "       sqcalc --collect VAR EXPR\n"
            "       sqcalc --solve-for VAR EQUATION      (exact: x^2 = 2 gives x = -sqrt(2) / x = sqrt(2))\n"
            "       sqcalc --solve VAR LO HI EXPR [name=value ...]\n"
            "       sqcalc --integrate VAR A B EXPR [name=value ...]\n"
            "       sqcalc --table VAR FROM TO STEP EXPR [name=value ...]\n"
            "Operators + - * / ^ ! and implied multiplication (2x, 3(x+1)); constants pi, e;\n"
            "functions sin cos tan asin acos atan sinh cosh tanh exp ln log sqrt cbrt abs\n"
            "floor ceil round sign, and atan2 hypot min max mod with two arguments.\n");
}

static void show(double x) {
    char b[64];
    sqm_format(x, digits, b, sizeof b);
    puts(b);
}

/* parse text, or print where it went wrong */
static int parse(const char *text, SqmExpr *e) {
    SqmError err;
    int rc = sqm_parse(text, e, &err);
    if (rc != SQM_OK) {
        fprintf(stderr, "sqcalc: %s\n  %s\n  %*s^\n", err.msg, text, err.pos, "");
        return -1;
    }
    return 0;
}

/* a constant expression (a bound or a value) */
static int number(const char *text, double *out) {
    static SqmExpr e;
    if (parse(text, &e)) return -1;
    if (sqm_eval(&e, vname, vval, nv, out) != SQM_OK) {
        fprintf(stderr, "sqcalc: %s: needs a value for a variable\n", text);
        return -1;
    }
    return 0;
}

/* name=value arguments from argv[i] on */
static int bindings(int argc, char **argv, int i) {
    for (; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq || eq == argv[i] || nv >= MAXV) {
            fprintf(stderr, "sqcalc: expected name=value, got %s\n", argv[i]);
            return 2;
        }
        *eq = 0;
        double v;
        if (number(eq + 1, &v)) return 1;
        vname[nv] = argv[i];
        vval[nv++] = v;
    }
    return 0;
}

static int unknown(const SqmExpr *e) {
    for (int k = 0; k < e->nnames; k++) {
        int bound = 0;
        for (int i = 0; i < nv; i++) bound |= strcmp(vname[i], e->names[k]) == 0;
        if (!bound) {
            fprintf(stderr, "sqcalc: no value for %s\n", e->names[k]);
            return 1;
        }
    }
    return 0;
}

/* compile with var in slot 0 and the bindings after it */
static int program(const SqmExpr *e, const char *var, SqmProgram *p, double *values) {
    const char *slots[MAXV + 1];
    slots[0] = var;
    values[0] = 0;
    for (int i = 0; i < nv; i++) {
        slots[i + 1] = vname[i];
        values[i + 1] = vval[i];
    }
    int rc = sqm_compile(e, slots, nv + 1, p);
    if (rc == SQM_ERR_UNKNOWN) {
        for (int k = 0; k < e->nnames; k++) {
            int bound = strcmp(e->names[k], var) == 0;
            for (int i = 0; i < nv; i++) bound |= strcmp(vname[i], e->names[k]) == 0;
            if (!bound) fprintf(stderr, "sqcalc: no value for %s\n", e->names[k]);
        }
        return 1;
    }
    if (rc != SQM_OK) {
        fprintf(stderr, "sqcalc: %s\n", sqm_strerror(rc));
        return 1;
    }
    return 0;
}

static SqmExpr ex, ex2;
static SqmProgram prog;

int main(int argc, char **argv) {
    int i = 1;
    if (i < argc && strcmp(argv[i], "--digits") == 0) {
        if (i + 1 >= argc) return usage(stderr), 2;
        digits = atoi(argv[i + 1]);
        if (digits < 1 || digits > 17) return fprintf(stderr, "sqcalc: --digits is 1..17\n"), 2;
        i += 2;
    }
    if (i >= argc) return usage(stderr), 2;
    const char *mode = argv[i];
    if (!strcmp(mode, "--help") || !strcmp(mode, "-h")) return usage(stdout), 0;

    if (!strcmp(mode, "--simplify")) {
        if (i + 2 != argc) return usage(stderr), 2;
        if (parse(argv[i + 1], &ex)) return 1;
        int rc = sqm_simplify(&ex, &ex2);
        if (rc) return fprintf(stderr, "sqcalc: %s\n", sqm_strerror(rc)), 1;
        char b[4096];
        sqm_print(&ex2, b, sizeof b);
        puts(b);
        return 0;
    }
    if (!strcmp(mode, "--expand") || !strcmp(mode, "--collect")) {
        int collect = mode[2] == 'c';
        if (i + 2 + collect != argc) return usage(stderr), 2;
        if (parse(argv[i + 1 + collect], &ex)) return 1;
        int rc = collect ? sqm_collect(&ex, argv[i + 1], &ex2) : sqm_expand(&ex, &ex2);
        if (rc) return fprintf(stderr, "sqcalc: %s\n", sqm_strerror(rc)), 1;
        char b[4096];
        sqm_print(&ex2, b, sizeof b);
        puts(b);
        return 0;
    }
    if (!strcmp(mode, "--solve-for")) {
        if (i + 3 != argc) return usage(stderr), 2;
        const char *var = argv[i + 1], *text = argv[i + 2];
        SqmError err;
        if (sqm_parse_eq(text, &ex, &err) != SQM_OK) {
            fprintf(stderr, "sqcalc: %s\n  %s\n  %*s^\n", err.msg, text, err.pos, "");
            return 1;
        }
        SqmSolution sol[32];
        int n, rc = sqm_solve_for(&ex, var, sol, 32, &n);
        if (n < 0) printf("every %s\n", var);
        else if (n == 0 && rc == SQM_OK) printf("no real solution\n");
        for (int k = 0; k < n; k++) {
            char v[64];
            sqm_format(sol[k].value, digits, v, sizeof v);
            if (sol[k].value == sol[k].value && strcmp(v, sol[k].text)) printf("%s = %s  (%s)\n", var, sol[k].text, v);
            else printf("%s = %s\n", var, sol[k].text);
        }
        if (rc) return fprintf(stderr, "sqcalc: %s\n", sqm_strerror(rc)), 1;
        return 0;
    }
    if (!strcmp(mode, "--diff")) {
        if (i + 3 > argc) return usage(stderr), 2;
        const char *var = argv[i + 1];
        int rc = bindings(argc, argv, i + 3);
        if (rc) return rc;
        if (parse(argv[i + 2], &ex)) return 1;
        rc = sqm_diff(&ex, var, &ex2);
        if (rc) return fprintf(stderr, "sqcalc: %s\n", sqm_strerror(rc)), 1;
        char b[4096];
        sqm_print(&ex2, b, sizeof b);
        puts(b);
        if (nv) { /* and its value there */
            double v;
            if (unknown(&ex2) || sqm_eval(&ex2, vname, vval, nv, &v)) return 1;
            show(v);
        }
        return 0;
    }
    if (!strcmp(mode, "--solve") || !strcmp(mode, "--integrate")) {
        if (i + 5 > argc) return usage(stderr), 2;
        const char *var = argv[i + 1];
        int rc = bindings(argc, argv, i + 5);
        if (rc) return rc;
        double lo, hi, values[MAXV + 1];
        if (number(argv[i + 2], &lo) || number(argv[i + 3], &hi)) return 1;
        if (parse(argv[i + 4], &ex) || program(&ex, var, &prog, values)) return 1;
        if (!strcmp(mode, "--integrate")) {
            int ok;
            double v = sqm_integrate(&prog, values, 0, lo, hi, 1e-12, &ok);
            show(v);
            if (!ok) fprintf(stderr, "sqcalc: the integral may not have converged\n");
            return 0;
        }
        double r[64];
        int n = sqm_roots(&prog, values, 0, lo, hi, 2000, r, 64);
        if (n == 0) fprintf(stderr, "sqcalc: no sign change in [%g, %g]\n", lo, hi);
        for (int k = 0; k < n && k < 64; k++) {
            printf("%s = ", var);
            show(r[k]);
        }
        return 0;
    }
    if (!strcmp(mode, "--table")) {
        if (i + 6 > argc) return usage(stderr), 2;
        const char *var = argv[i + 1];
        int rc = bindings(argc, argv, i + 6);
        if (rc) return rc;
        double from, to, step, values[MAXV + 1];
        if (number(argv[i + 2], &from) || number(argv[i + 3], &to) || number(argv[i + 4], &step)) return 1;
        if (!(step > 0) || !(to >= from) || (to - from) / step > 100000)
            return fprintf(stderr, "sqcalc: need FROM <= TO and a STEP > 0 (at most 100000 rows)\n"), 2;
        if (parse(argv[i + 5], &ex) || program(&ex, var, &prog, values)) return 1;
        long rows = (long)((to - from) / step + 1e-9);
        for (long k = 0; k <= rows; k++) {
            double x = from + step * (double)k;
            values[0] = x;
            char a[64], b[64];
            sqm_format(x, digits, a, sizeof a);
            sqm_format(sqm_run(&prog, values), digits, b, sizeof b);
            printf("%s\t%s\n", a, b);
        }
        return 0;
    }
    if (mode[0] == '-' && mode[1] == '-') return usage(stderr), 2;

    int rc = bindings(argc, argv, i + 1);
    if (rc) return rc;
    if (parse(mode, &ex) || unknown(&ex)) return 1;
    double v;
    if (sqm_eval(&ex, vname, vval, nv, &v)) return 1;
    show(v);
    return 0;
}
